#include "GloveDevice.h"
#include <SPIFFS.h>

// helpers
static inline void softShiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value) {
  for (int i = 0; i < 8; i++) {
    uint8_t bitValue = (bitOrder == MSBFIRST)
      ? ((value & (1 << (7 - i))) ? HIGH : LOW)
      : ((value & (1 << i)) ? HIGH : LOW);
    digitalWrite(dataPin, bitValue);
    digitalWrite(clockPin, HIGH);
    delayMicroseconds(SOFT_SHIFT_DELAY_US);
    digitalWrite(clockPin, LOW);
    delayMicroseconds(SOFT_SHIFT_DELAY_US);
  }
}

static inline uint32_t readCapacitance(gpio_num_t pin) {
  gpio_reset_pin(pin);
  gpio_set_direction(pin, GPIO_MODE_OUTPUT);
  gpio_set_level(pin, 0);
  ets_delay_us(CAPACITANCE_DELAY_US);
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

void GloveDevice::showStatus(StatusStage stage) {
  if (!ringReady) beginRingOnly();
  switch (stage) {
    case StatusStage::Boot:
      ring.blink(CRGB(240, 140, 20), 120, 120, 2); // amber blink
      ringResumeAt = millis() + 520;
      break;
    case StatusStage::WifiConnecting:
      ring.ring(CRGB(50, 120, 255), 140); // deep blue ring
      ringResumeAt = 0;
      break;
    case StatusStage::WifiConnected:
      ring.pulse(CRGB(40, 200, 90), 160, 220, 3); // fresh green pulse
      ringResumeAt = millis() + 1100;
      break;
    case StatusStage::Hotspot:
      ring.ring(CRGB(190, 60, 200), 110); // magenta ring
      ringResumeAt = millis() + 900;
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
  ring.pulse(CRGB(90, 180, 255), 180, 220, 1); // cool blue pulse for inbound
  ringResumeAt = millis() + 520;
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

void GloveDevice::startCapture(char symbol) {
  captureActive = true;
  captureSymbol = symbol;
  captureSequence.clear();
  sendCaptureEvent("start");
}

void GloveDevice::cancelCapture() {
  captureActive = false;
  captureSequence.clear();
  sendCaptureEvent("cancel");
}

void GloveDevice::saveCapture() {
  if (!captureActive || captureSequence.empty()) return;
  std::vector<GestureStep> steps;
  for (auto mask : captureSequence) steps.push_back(GestureStep{mask, DEFAULT_STEP_ON_MS, DEFAULT_STEP_OFF_MS});
  store.updateGesture(captureSymbol, steps);
  captureActive = false;
  sendAlphabet();
  sendCaptureEvent("saved");
}

void GloveDevice::sendAlphabet() {
  StaticJsonDocument<2048> doc;
  doc["type"] = "alphabet";
  store.asJson(doc);
  push(doc);
}

bool GloveDevice::importGestures(JsonArray arr) {
  bool ok = store.importGestures(arr);
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
  delayMicroseconds(LATCH_DELAY_US);
  softShiftOut(PIN_DATA, PIN_CLOCK, MSBFIRST, (uint8_t)(mask >> 8));
  softShiftOut(PIN_DATA, PIN_CLOCK, MSBFIRST, (uint8_t)(mask & 0xFF));
  delayMicroseconds(LATCH_DELAY_US);
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
    sendMessageEvent("outbound", typingBuffer);
    typingBuffer = "";
    sendTypingEvent("done");
    ring.blink(CRGB::Green, 120, 120, 1);
    ringResumeAt = millis() + 400;
    return;
  }
  if (player.isActive()) {
    if (playbackState == 0) {
      player.setTimings(timingOn * 2, timingOff * 2, timingGap * 2);
      player.restartLast(store, true);
      playbackState = 1;
      ring.fadeTo(CRGB::Yellow, 200, true);
    } else if (playbackState == 1) {
      player.clear();
      player.setTimings(timingOn, timingOff, timingGap);
      playbackState = 2;
      ring.fadeTo(CRGB::Red, 200, true);
    } else {
      player.setTimings(timingOn, timingOff, timingGap);
      player.restartLast(store, false);
      playbackState = 0;
      ring.fadeTo(CRGB::Green, 200, true);
    }
    return;
  }
  if (!player.lastMessageText().isEmpty() && playbackState == 2) {
    player.restartLast(store, false);
    playbackState = 0;
  } else if (player.lastMessageText().isEmpty()) {
    pulseOutput(0, 100);
  }
}

void GloveDevice::readInputs(uint32_t now) {
  if (captureActive) {
    handleCapture(now);
    return;
  }
  if (mode == ModeTouchFeedback && (now < inputLockUntil)) return;

  uint32_t mask = scanInputs();
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

  if (mask != stableMask && (now - lastMaskChange) >= GESTURE_STABLE_MS) {
    stableMask = mask;
    if (stableMask != 0) {
      currentSequence.push_back((uint16_t)stableMask);
      lastStepTime = now;
      sendGestureState();
      showGestureProgress();
      char matched = store.fullMatch(currentSequence);
      if (matched != '\0') {
        handleSymbol(matched);
        currentSequence.clear();
        showIdleRing();
      } else {
        sendTypingEvent("typing");
      }
    }
  }
  if (!currentSequence.empty() && (now - lastStepTime) > GESTURE_TIMEOUT_MS) {
    vibrateError();
    currentSequence.clear();
    sendGestureState(true);
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
    delayMicroseconds(MUX_DELAY_US);
    highs += readAndWrite(bits, i);
    highs += readAndWrite(bits, i + 8);
  }
  highInputs = highs;
  return bits;
}

uint8_t GloveDevice::readAndWrite(uint32_t& bits, uint8_t index) {
  gpio_num_t pin = (index < 8) ? (gpio_num_t)PIN_Z0 : (gpio_num_t)PIN_Z1;
  uint32_t cycles = readCapacitance(pin);
  capacitanceValues[index] = cycles;
  bool hit = cycles >= touchThreshold;
  bitWrite(bits, index, hit);
  return hit ? 1 : 0;
}

void GloveDevice::handleCapture(uint32_t now) {
  uint32_t mask = scanInputs();
  if (mask != captureLastMask) {
    captureLastChange = now;
    captureLastMask = mask;
  }
  if (mask != captureStable && (now - captureLastChange) >= GESTURE_STABLE_MS) {
    captureStable = mask;
    if (captureStable != 0) {
      captureSequence.push_back((uint16_t)captureStable);
      sendCaptureEvent("step");
    }
  }
}

void GloveDevice::handleSymbol(char symbol) {
  char s = (symbol == 'W') ? symbol : (char)tolower(symbol);
  if (s == 'W') {
    pushSymbol(s);
    if (!typingBuffer.isEmpty()) {
      sendMessageEvent("outbound", typingBuffer);
      typingBuffer = "";
      sendTypingEvent("done");
    }
    return;
  }
  pushSymbol(s);
  typingBuffer += s;
  sendTypingEvent("typing");
}

void GloveDevice::vibrateError() {
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
  StaticJsonDocument<256> doc;
  doc["type"] = "status";
  doc["reason"] = reason;
  doc["mode"] = mode;
  doc["threshold"] = touchThreshold;
  doc["inputs"] = highInputs;
  doc["playback_active"] = player.isActive();
  doc["playback_state"] = playbackState;
  doc["buffer"] = typingBuffer;
  doc["debug_streaming"] = debugStreaming;
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

void GloveDevice::sendCaptureEvent(const char* event) {
  StaticJsonDocument<256> doc;
  doc["type"] = "capture";
  doc["event"] = event;
  doc["symbol"] = String(captureSymbol);
  JsonArray seq = doc.createNestedArray("steps");
  for (auto m : captureSequence) seq.add(m);
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
  if (!currentSequence.empty()) {
    if (lastSequenceVisualized != currentSequence.size()) showGestureProgress();
  } else if (!idleRingActive || (now - lastInteraction) > GESTURE_TIMEOUT_MS) {
    showIdleRing();
  }
}

void GloveDevice::showIdleRing() {
  if (!ringReady) return;
  ring.ring(CRGB(30, 190, 200), 160); // soft teal
  idleRingActive = true;
  lastSequenceVisualized = 0;
  ringResumeAt = 0;
}

void GloveDevice::showGestureProgress() {
  if (!ringReady) return;
  if (currentSequence.empty()) { showIdleRing(); return; }
  idleRingActive = false;
  ring.customClear(CRGB::Black);
  size_t capped = std::min(currentSequence.size(), (size_t)RGB_CHAIN_LEN);
  for (size_t i = 0; i < capped; i++) {
    if (i + 1 == capped) ring.setPixelBlink(i, CRGB(255, 170, 40), 180, 180, 0);
    else ring.setPixel(i, CRGB(40, 160, 220));
  }
  lastSequenceVisualized = capped;
  ringResumeAt = 0;
}

void GloveDevice::showModeIndicator(uint8_t modeIndex) {
  if (!ringReady) return;
  uint8_t led = modeIndex % RGB_CHAIN_LEN;
  ring.customClear(CRGB::Black);
  ring.setPixelBlink(led, CRGB::Red, 140, 120, 2);
  ringResumeAt = millis() + (140 + 120) * 2 + 160;
  idleRingActive = false;
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
