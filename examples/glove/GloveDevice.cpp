#include "GloveDevice.h"
#include <SPIFFS.h>

// helpers
static inline void softShiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value, uint16_t delayUs) {
  for (int i = 0; i < 8; i++) {
    uint8_t bitValue = (bitOrder == MSBFIRST)
      ? ((value & (1 << (7 - i))) ? HIGH : LOW)
      : ((value & (1 << i)) ? HIGH : LOW);
    digitalWrite(dataPin, bitValue);
    digitalWrite(clockPin, HIGH);
    delayMicroseconds(delayUs);
    digitalWrite(clockPin, LOW);
    delayMicroseconds(delayUs);
  }
}

static inline uint32_t readCapacitance(gpio_num_t pin, uint16_t delayUs) {
  gpio_reset_pin(pin);
  gpio_set_direction(pin, GPIO_MODE_OUTPUT);
  gpio_set_level(pin, 0);
  ets_delay_us(delayUs);
  gpio_set_direction(pin, GPIO_MODE_INPUT);

  uint32_t cycles = 0;
  while (gpio_get_level(pin) == 0) {
    cycles++;
    if (cycles > 100) {
      return 100;
    }
  }
  return cycles;
}

void GloveDevice::beginRingOnly() {
  ring.begin();
  ringReady = true;
  idleRingActive = false;
  lastSequenceVisualized = 0;
}

// =============================================================================
// LED RING ANIMATION SYSTEM
// =============================================================================
// Color Palette (distinct, accessible colors):
//   - CYAN (30,190,200)    : Idle/Ready state - calm, neutral
//   - BLUE (50,120,255)    : WiFi/Network activity
//   - GREEN (40,220,80)    : Success/Confirmation
//   - ORANGE (255,140,30)  : Boot/Warning/Attention
//   - MAGENTA (200,50,180) : Hotspot/AP mode
//   - RED (255,50,30)      : Error/Stop
//   - YELLOW (255,200,40)  : Slow mode/Caution
//   - AMBER (255,170,40)   : Gesture input active
//   - WHITE (200,200,200)  : Neutral feedback
//
// Animation Types:
//   - ring()  : Rotating LED - ongoing process (WiFi connecting, waiting)
//   - pulse() : Fade in/out - transient event (message received)
//   - blink() : On/off flash - attention/confirmation
//   - solid() : Static color - stable state
//   - progress() : Fill bar - progress indication (OTA)
// =============================================================================

void GloveDevice::showStatus(StatusStage stage) {
  if (!ringReady) beginRingOnly();
  idleRingActive = false;
  switch (stage) {
    case StatusStage::Boot:
      // Orange pulse - device starting up
      ring.pulse(CRGB(255, 140, 30), 300, 200, 2);
      ringResumeAt = millis() + 1100;
      break;
    case StatusStage::WifiConnecting:
      // Blue rotating - searching for network
      ring.ring(CRGB(50, 120, 255), 120);
      ringResumeAt = 0;  // Continues until connected
      break;
    case StatusStage::WifiConnected:
      // Green pulse - connection successful
      ring.pulse(CRGB(40, 220, 80), 250, 350, 2);
      ringResumeAt = millis() + 1300;
      break;
    case StatusStage::Hotspot:
      // Magenta rotating - AP mode active
      ring.ring(CRGB(200, 50, 180), 100);
      ringResumeAt = 0;  // Continues while in hotspot
      break;
    case StatusStage::Ready:
    default:
      showIdleRing();
      break;
  }
}

void GloveDevice::tickRing(uint32_t now) {
  if (ringResumeAt != 0 && now >= ringResumeAt) {
    ringResumeAt = 0;
    applyBaseRingState(now);
  }
  ring.tick(now);
}

bool GloveDevice::begin() {
  if (!ringReady) beginRingOnly();

  pinMode(PIN_MOSFET, OUTPUT);
  digitalWrite(PIN_MOSFET, LOW);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  pinMode(PIN_LATCH, OUTPUT);
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_CLOCK, OUTPUT);
  pinMode(PIN_S0, OUTPUT);
  pinMode(PIN_S1, OUTPUT);
  pinMode(PIN_S2, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  shiftZero();

  if (!store.begin()) {
    log("Falha ao inicializar SPIFFS ou gestos");
    return false;
  }
  player.setTimings(timingOn, timingOff, timingGap);
  player.setOutputWriter([&](uint16_t mask){ writeOutputs(mask); });

  pulseOutput(0, 100);
  sendStatus("boot");
  lastInteraction = millis();
  showIdleRing();
  return true;
}

void GloveDevice::tick() {
  uint32_t now = millis();
  handleButton(now);

  if (mode == ModeSequencer) runSequencer(now);
  else readInputs(now);

  player.tick(now);
  tickRing(now);
}

void GloveDevice::onMessageFromWeb(const String& text) {
  String clean = normalize(text);
  if (clean.isEmpty()) return;
  // Blue pulse - incoming message from web
  ring.pulse(CRGB(50, 120, 255), 200, 300, 1);
  ringResumeAt = millis() + 600;
  playbackState = 0;
  player.setTimings(timingOn, timingOff, timingGap);
  player.queueMessage(clean, store);
  sendMessageEvent("inbound", clean);
}

void GloveDevice::setMode(Mode m) {
  uint8_t idx = (uint8_t)m;
  if (idx >= RGB_CHAIN_LEN) idx = RGB_CHAIN_LEN - 1;
  mode = (Mode)idx;
  sendStatus("mode");
  pulseOutput(idx, 100);
  showModeIndicator(idx);
}

void GloveDevice::setThreshold(uint8_t t) {
  touchThreshold = t;
  sendStatus("threshold");
}

void GloveDevice::setTimings(uint16_t onMs, uint16_t offMs, uint16_t gapMs) {
  timingOn = onMs;
  timingOff = offMs;
  timingGap = gapMs;
  player.setTimings(onMs, offMs, gapMs);
  sendStatus("timings");
}

void GloveDevice::setHardwareTimings(uint16_t latchUs, uint16_t softShiftUs, uint16_t muxUs, uint16_t capacitanceUs) {
  // Validate timing values
  if (latchUs < MIN_LATCH_DELAY_US || latchUs > MAX_LATCH_DELAY_US ||
      softShiftUs < MIN_SOFT_SHIFT_DELAY_US || softShiftUs > MAX_SOFT_SHIFT_DELAY_US ||
      muxUs < MIN_MUX_DELAY_US || muxUs > MAX_MUX_DELAY_US ||
      capacitanceUs < MIN_CAPACITANCE_DELAY_US || capacitanceUs > MAX_CAPACITANCE_DELAY_US) {
    log("Invalid hardware timing values - rejected");
    return;
  }

  latchDelayUs = latchUs;
  softShiftDelayUs = softShiftUs;
  muxDelayUs = muxUs;
  capacitanceDelayUs = capacitanceUs;
  sendStatus("hardware_timings");
}

void GloveDevice::setAnimation(const String& name, uint32_t rgb, uint16_t speedMs, uint8_t count) {
  CRGB color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
  if (name == "off") ring.off();
  else if (name == "solid") ring.solid(color);
  else if (name == "fade-in") ring.fadeTo(color, speedMs, true);
  else if (name == "fade-out") ring.fadeTo(color, speedMs, false);
  else if (name == "blink") ring.blink(color, speedMs, speedMs, count);
  else if (name == "pulse") ring.pulse(color, speedMs, speedMs, count);
  else if (name == "ring") ring.ring(color, speedMs);
}

void GloveDevice::setDebugStreaming(bool enabled) {
  debugStreaming = enabled;
  if (debugStreaming) {
    publishInputs(stableMask);
    sendOutput(lastOutputMask);
  }
  sendStatus("debug_stream");
}

void GloveDevice::sendAlphabet() {
  DynamicJsonDocument doc(6144);
  doc["type"] = "alphabet";

  // Create gestures array for UI compatibility
  JsonArray gesturesArray = doc.createNestedArray("gestures");

  // Get the simplified format from store
  DynamicJsonDocument simplifiedDoc(4096);
  store.asJson(simplifiedDoc);

  // Convert simplified format to array format for UI
  JsonObject simplifiedObj = simplifiedDoc.as<JsonObject>();
  for (JsonPair kv : simplifiedObj) {
    JsonObject gestureObj = gesturesArray.createNestedObject();
    gestureObj["symbol"] = kv.key().c_str();
    JsonArray stepsArray = gestureObj.createNestedArray("steps");

    JsonArray maskArray = kv.value().as<JsonArray>();
    for (JsonVariant maskValue : maskArray) {
      JsonObject stepObj = stepsArray.createNestedObject();
      stepObj["mask"] = maskValue;
    }
  }

  push(doc);
}

bool GloveDevice::importGestures(JsonObject obj) {
  bool ok = store.importGestures(obj);
  if (ok) sendAlphabet();
  return ok;
}

bool GloveDevice::resetToDefaults() {
  bool ok = store.resetToDefaults();
  if (ok) sendAlphabet();
  return ok;
}

void GloveDevice::playLetter(char symbol) {
  const auto* g = store.get(symbol);
  if (!g) return;
  player.queueGesture(*g);
}

void GloveDevice::requestState() { sendStatus("manual"); }

void GloveDevice::shiftZero() { writeOutputs(0); }

void GloveDevice::writeOutputs(uint16_t mask) {
  digitalWrite(PIN_LATCH, LOW);
  delayMicroseconds(latchDelayUs);
  softShiftOut(PIN_DATA, PIN_CLOCK, MSBFIRST, (uint8_t)(mask >> 8), softShiftDelayUs);
  softShiftOut(PIN_DATA, PIN_CLOCK, MSBFIRST, (uint8_t)(mask & 0xFF), softShiftDelayUs);
  delayMicroseconds(latchDelayUs);
  digitalWrite(PIN_LATCH, HIGH);
  if (mask != lastOutputMask) {
    lastOutputMask = mask;
    sendOutput(mask);
  }
}

void GloveDevice::pulseOutput(uint8_t bit, uint16_t ms) {
  uint16_t mask = (bit < 16) ? (1 << bit) : 0;
  writeOutputs(mask);
  delay(ms);
  writeOutputs(0);
}

void GloveDevice::handleButton(uint32_t now) {
  bool pressed = digitalRead(PIN_BUTTON) == HIGH;
  if (pressed != lastButtonState && (now - lastButtonChange) > 20) {
    lastButtonChange = now;
    lastButtonState = pressed;
    if (!pressed) {
      clickCount++;
      clickDeadline = now + 450;
    }
  }
  if (clickCount > 0 && now > clickDeadline) {
    if (clickCount >= 2) {
      Mode next = (Mode)((mode + 1) % RGB_CHAIN_LEN);
      setMode(next);
    } else {
      handleSingleClick();
    }
    clickCount = 0;
  }
}

void GloveDevice::handleSingleClick() {
  if (!typingBuffer.isEmpty()) {
    // Green pulse - message sent successfully
    sendMessageEvent("outbound", typingBuffer);
    typingBuffer = "";
    sendTypingEvent("done");
    ring.pulse(CRGB(40, 220, 80), 150, 250, 2);
    ringResumeAt = millis() + 900;
    return;
  }
  if (player.isActive()) {
    if (playbackState == 0) {
      // Yellow solid - slow playback mode
      player.setTimings(timingOn * 2, timingOff * 2, timingGap * 2);
      player.restartLast(store, true);
      playbackState = 1;
      ring.solid(CRGB(255, 200, 40));
      ringResumeAt = 0;  // Stays until state changes
    } else if (playbackState == 1) {
      // Red solid - playback paused
      player.clear();
      player.setTimings(timingOn, timingOff, timingGap);
      playbackState = 2;
      ring.solid(CRGB(255, 50, 30));
      ringResumeAt = 0;
    } else {
      // Green pulse - resuming normal playback
      player.setTimings(timingOn, timingOff, timingGap);
      player.restartLast(store, false);
      playbackState = 0;
      ring.pulse(CRGB(40, 220, 80), 200, 300, 1);
      ringResumeAt = millis() + 600;
    }
    return;
  }
  if (!player.lastMessageText().isEmpty() && playbackState == 2) {
    player.restartLast(store, false);
    playbackState = 0;
    ring.pulse(CRGB(40, 220, 80), 200, 300, 1);
    ringResumeAt = millis() + 600;
  } else if (player.lastMessageText().isEmpty()) {
    // White blink - no action available
    ring.blink(CRGB(200, 200, 200), 100, 100, 1);
    ringResumeAt = millis() + 300;
  }
}

void GloveDevice::readInputs(uint32_t now) {
  if (mode == ModeTouchFeedback && (now < inputLockUntil)) return;

  uint32_t rawMask = scanInputs();

  // Debounce: manter última máscara não-zero por um curto período
  uint32_t mask = rawMask;
  if (mask == 0 && lastNonZeroMask != 0) {
    if (zeroStartTime == 0) {
      zeroStartTime = now;
    }
    if ((now - zeroStartTime) < (GESTURE_STABLE_MS / 2)) {
      mask = lastNonZeroMask;
    }
  } else if (mask != 0) {
    lastNonZeroMask = mask;
    zeroStartTime = 0;
  }

  if (mask != lastRawMask) {
    lastRawMask = mask;
    lastMaskChange = now;
  }
  if (mask != 0) lastInteraction = now;
  publishInputs(mask);
  if (mask == 0 && lastNotifiedMask != 0) {
    sendNoInput();
    lastNotifiedMask = 0;
  } else if (mask != 0) lastNotifiedMask = mask;

  if (mode == ModeTouchFeedback) {
    if (mask != 0 && now - lastTouchFeedback > 80) {
      player.clear();
      writeOutputs(mask);
      touchFeedbackOffAt = now + 100;
      inputLockUntil = now + 100;
      lastTouchFeedback = now;
    } else if (touchFeedbackOffAt != 0 && now >= touchFeedbackOffAt) {
      writeOutputs(0);
      touchFeedbackOffAt = 0;
    }
    return;
  }

  // Quando usuário solta todas as teclas, resetar stableMask para permitir novo ciclo
  if (mask == 0 && stableMask != 0) {
    stableMask = 0;
  }

  // Lógica principal de detecção de gestos
  // Só processar novo toque se: máscara mudou E está estável E é diferente de zero
  if (mask != 0 && mask != stableMask && (now - lastMaskChange) >= GESTURE_STABLE_MS) {
    stableMask = mask;

    // Novo toque detectado - adicionar à sequência
    currentSequence.push_back((uint16_t)stableMask);
    lastStepTime = now;
    sendGestureState();
    showGestureProgress();

    // Verificar candidatos
    auto prefixCandidates = store.prefixMatches(currentSequence);
    auto exactCandidates = store.exactLengthMatches(currentSequence);

    // Verificar se há candidatos com mais steps (sequências mais longas)
    bool hasLongerCandidates = prefixCandidates.size() > exactCandidates.size();

    if (prefixCandidates.empty()) {
      // Nenhum candidato - sequência inválida
      vibrateError();
      currentSequence.clear();
      candidateConfirmTime = 0;
      lastCandidates.clear();
      sendGestureState(true);
      showIdleRing();
    } else if (exactCandidates.size() == 1 && !hasLongerCandidates) {
      // Único candidato exato e SEM candidatos mais longos - confirmar imediatamente
      handleSymbol(exactCandidates[0]);
      currentSequence.clear();
      candidateConfirmTime = 0;
      lastCandidates.clear();
      showIdleRing();
    } else if (!exactCandidates.empty()) {
      // Há candidatos exatos MAS também há prefixos mais longos
      // Iniciar/reiniciar timeout para confirmação
      candidateConfirmTime = now + GESTURE_CONFIRM_MS;
      lastCandidates = exactCandidates;
      sendTypingEvent("typing");
    } else {
      // Só há prefixos mais longos, aguardar mais input
      // Cancelar qualquer timeout pendente
      candidateConfirmTime = 0;
      lastCandidates.clear();
      sendTypingEvent("typing");
    }
  }

  // Verificar timeout de confirmação (quando há múltiplos candidatos ou candidatos mais longos)
  if (candidateConfirmTime != 0 && now >= candidateConfirmTime && !lastCandidates.empty()) {
    // Timeout expirou sem novo input - confirmar o primeiro candidato exato
    handleSymbol(lastCandidates[0]);
    currentSequence.clear();
    candidateConfirmTime = 0;
    lastCandidates.clear();
    showIdleRing();
  }

  // Timeout geral da sequência (usuário parou de digitar)
  if (!currentSequence.empty() && (now - lastStepTime) > GESTURE_TIMEOUT_MS) {
    // Verificar se há um match exato antes de descartar
    auto exactCandidates = store.exactLengthMatches(currentSequence);
    if (!exactCandidates.empty()) {
      handleSymbol(exactCandidates[0]);
    } else {
      vibrateError();
      sendGestureState(true);
    }
    currentSequence.clear();
    candidateConfirmTime = 0;
    lastCandidates.clear();
    showIdleRing();
  }

  if (mask == 0 && currentSequence.empty() && (now - lastInteraction) > GESTURE_TIMEOUT_MS) {
    showIdleRing();
  }
}

uint32_t GloveDevice::scanInputs() {
  int r0, r1, r2;
  uint32_t bits = 0;
  uint8_t highs = 0;
  for (int i = 0; i < 8; i++) {
    r0 = bitRead(i, 0);
    r1 = bitRead(i, 1);
    r2 = bitRead(i, 2);
    digitalWrite(PIN_S0, r0);
    digitalWrite(PIN_S1, r1);
    digitalWrite(PIN_S2, r2);
    delayMicroseconds(muxDelayUs);
    highs += readAndWrite(bits, i);
    highs += readAndWrite(bits, i + 8);
  }
  highInputs = highs;
  return bits;
}

uint8_t GloveDevice::readAndWrite(uint32_t& bits, uint8_t index) {
  gpio_num_t pin = (index < 8) ? (gpio_num_t)PIN_Z0 : (gpio_num_t)PIN_Z1;
  uint32_t cycles = readCapacitance(pin, capacitanceDelayUs);
  capacitanceValues[index] = cycles;
  bool hit = cycles >= touchThreshold;
  bitWrite(bits, index, hit);
  return hit ? 1 : 0;
}

void GloveDevice::handleSymbol(char symbol) {
  if (symbol == 'W') {
    pushSymbol(symbol);
    if (!typingBuffer.isEmpty()) {
      sendMessageEvent("outbound", typingBuffer);
      typingBuffer = "";
      sendTypingEvent("done");
    }
    return;
  }
  pushSymbol(symbol);
  typingBuffer += symbol;
  sendTypingEvent("typing");
}

void GloveDevice::vibrateError() {
  // Red blink - error/invalid input
  ring.blink(CRGB(255, 50, 30), 80, 80, 3);
  ringResumeAt = millis() + 550;
  const auto* g = store.get('w');
  if (g) player.queueGesture(*g);
}

void GloveDevice::runSequencer(uint32_t now) {
  if (now < sequencerNext) return;
  if (sequencerOn) {
    writeOutputs(0);
    sequencerOn = false;
    sequencerNext = now + SEQ_OFF_MS;
    sequencerIndex = (sequencerIndex + 1) % 16;
  } else {
    writeOutputs(1 << sequencerIndex);
    sequencerOn = true;
    sequencerNext = now + SEQ_ON_MS;
  }
}

void GloveDevice::sendStatus(const char* reason) {
  StaticJsonDocument<512> doc;
  doc["type"] = "status";
  doc["reason"] = reason;
  doc["mode"] = mode;
  doc["threshold"] = touchThreshold;
  doc["inputs"] = highInputs;
  doc["playback_active"] = player.isActive();
  doc["playback_state"] = playbackState;
  doc["buffer"] = typingBuffer;
  doc["debug_streaming"] = debugStreaming;
  doc["free_memory"] = ESP.getFreeHeap();

  // Include hardware timing values
  JsonObject timings = doc.createNestedObject("hardware_timings");
  timings["latch_delay_us"] = latchDelayUs;
  timings["soft_shift_delay_us"] = softShiftDelayUs;
  timings["mux_delay_us"] = muxDelayUs;
  timings["capacitance_delay_us"] = capacitanceDelayUs;

  push(doc);
}

void GloveDevice::sendMessageEvent(const char* dir, const String& txt) {
  StaticJsonDocument<256> doc;
  doc["type"] = "message";
  doc["direction"] = dir;
  doc["text"] = txt;
  push(doc);
}

void GloveDevice::sendTypingEvent(const char* state) {
  StaticJsonDocument<256> doc;
  doc["type"] = "typing";
  doc["state"] = state;
  doc["buffer"] = typingBuffer;
  push(doc);
}

void GloveDevice::sendGestureState(bool expired) {
  StaticJsonDocument<512> doc;
  doc["type"] = "gesture_state";
  doc["expired"] = expired;
  JsonArray seq = doc.createNestedArray("sequence");
  for (auto m : currentSequence) seq.add(m);
  JsonArray candidates = doc.createNestedArray("candidates");
  auto matches = store.prefixMatches(currentSequence);
  for (auto c : matches) candidates.add(String(c));
  push(doc);
}

void GloveDevice::publishInputs(uint32_t mask) {
  if (!debugStreaming) return;
  StaticJsonDocument<512> doc;
  doc["type"] = "input";
  doc["raw"] = mask;
  JsonArray pins = doc.createNestedArray("pins");
  for (int i = 0; i < 16; i++) if (bitRead(mask, i)) pins.add(i);
  JsonArray cap = doc.createNestedArray("capacitance");
  for (int i = 0; i < 16; i++) cap.add(capacitanceValues[i]);
  push(doc);
}

void GloveDevice::sendNoInput() {
  if (!debugStreaming) return;
  StaticJsonDocument<128> doc;
  doc["type"] = "input_idle";
  push(doc);
}

void GloveDevice::sendOutput(uint16_t mask) {
  if (!debugStreaming) return;
  StaticJsonDocument<192> doc;
  doc["type"] = "output";
  doc["mask"] = mask;
  JsonArray pins = doc.createNestedArray("pins");
  for (int i = 0; i < 16; i++) if (bitRead(mask, i)) pins.add(i);
  push(doc);
}

void GloveDevice::push(JsonDocument& doc) {
  if (!send) return;
  String json;
  serializeJson(doc, json);
  send(json);
}

void GloveDevice::log(const char* msg) {
  StaticJsonDocument<192> doc;
  doc["type"] = "log";
  doc["msg"] = msg;
  push(doc);
  Serial.println(msg);
}

void GloveDevice::applyBaseRingState(uint32_t now) {
  if (!ringReady) return;
  // Don't interrupt temporary animations (pulse, blink, etc.)
  // They will finish and ringResumeAt will trigger this again
  if (ring.isAnimating()) return;

  if (!currentSequence.empty()) {
    if (lastSequenceVisualized != currentSequence.size()) showGestureProgress();
  } else {
    showIdleRing();
  }
}

void GloveDevice::showIdleRing() {
  if (!ringReady) return;
  if (idleRingActive && ring.isRing()) return;
  // Cyan rotating - idle, ready for input
  ring.ring(CRGB(30, 190, 200), 140);
  idleRingActive = true;
  lastSequenceVisualized = 0;
  ringResumeAt = 0;
}

void GloveDevice::showGestureProgress() {
  if (!ringReady) return;
  if (currentSequence.empty()) { showIdleRing(); return; }
  idleRingActive = false;
  // Amber for gesture input - show progress as filled LEDs
  ring.customClear(CRGB::Black);
  size_t capped = std::min(currentSequence.size(), (size_t)RGB_CHAIN_LEN);
  for (size_t i = 0; i < capped; i++) {
    if (i + 1 == capped) {
      // Current step pulses amber
      ring.setPixelPulse(i, CRGB(255, 170, 40), 300, 300, 0);
    } else {
      // Previous steps solid cyan
      ring.setPixel(i, CRGB(30, 190, 200));
    }
  }
  lastSequenceVisualized = capped;
  ringResumeAt = 0;
}

void GloveDevice::showModeIndicator(uint8_t modeIndex) {
  if (!ringReady) return;
  idleRingActive = false;
  // Show mode with distinct color per mode, pulse animation
  static const CRGB modeColors[] = {
    CRGB(30, 190, 200),   // Mode 0: Chat - Cyan
    CRGB(255, 140, 30),   // Mode 1: TouchFeedback - Orange
    CRGB(200, 50, 180),   // Mode 2: Sequencer - Magenta
    CRGB(50, 120, 255),   // Mode 3: Reserved - Blue
    CRGB(40, 220, 80),    // Mode 4: Reserved - Green
  };
  uint8_t led = modeIndex % RGB_CHAIN_LEN;
  CRGB color = modeColors[led];

  // Fill up to the mode LED, pulse the active one
  ring.customClear(CRGB::Black);
  for (uint8_t i = 0; i <= led; i++) {
    if (i == led) {
      ring.setPixelPulse(i, color, 250, 350, 3);
    } else {
      CRGB dim = color;
      dim.nscale8_video(60);
      ring.setPixel(i, dim);
    }
  }
  ringResumeAt = millis() + 1900;  // 3 pulses * ~600ms
}

String GloveDevice::normalize(const String& in) {
  String tmp = in;
  tmp.toLowerCase();
  tmp.replace("\xC3\xA1", "a");
  tmp.replace("\xC3\xA2", "a");
  tmp.replace("\xC3\xA3", "a");
  tmp.replace("\xC3\xA9", "e");
  tmp.replace("\xC3\xAA", "e");
  tmp.replace("\xC3\xAD", "i");
  tmp.replace("\xC3\xB3", "o");
  tmp.replace("\xC3\xB4", "o");
  tmp.replace("\xC3\xB5", "o");
  tmp.replace("\xC3\xBA", "u");
  tmp.replace("\xC3\xBC", "u");
  tmp.replace("\xC3\xA7", "c");
  String out;
  out.reserve(tmp.length());
  for (size_t i = 0; i < tmp.length(); i++) {
    char c = tmp[i];
    if (c >= 'a' && c <= 'z') out += c;
    else if (c == ' ') out += ' ';
    else if (c == 'w') out += 'w';
  }
  return out;
}

void GloveDevice::pushSymbol(char c) {
  StaticJsonDocument<128> doc;
  doc["type"] = "symbol";
  doc["value"] = String(c);
  push(doc);
}

void GloveDevice::showOtaProgress(uint8_t percent) {
  if (!ringReady) return;
  ring.progressPulse(percent, CRGB(50, 120, 255));
}

void GloveDevice::showOtaComplete() {
  if (!ringReady) return;
  ring.pulse(CRGB(40, 220, 80), 200, 300, 3);
}

void GloveDevice::showOtaError() {
  if (!ringReady) return;
  ring.blink(CRGB(255, 50, 30), 80, 80, 5);
}
