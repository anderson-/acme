#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include <algorithm>
#include "driver/gpio.h"
#include "esp_timer.h"

#include "GloveConfig.h"
#include "GestureStore.h"
#include "GesturePlayer.h"
#include "LedRingAnimator.h"

using SendFn = std::function<void(const String&)>;

class GloveDevice {
public:
  enum Mode : uint8_t { ModeChat = 0, ModeTouchFeedback = 1, ModeSequencer = 2, ModeReserved3 = 3, ModeReserved4 = 4, ModeReserved5 = 5 };
  enum class StatusStage : uint8_t { Boot, WifiConnecting, WifiConnected, Ready, Hotspot };

  void setSender(SendFn fn) { send = fn; }
  void beginRingOnly();
  void showStatus(StatusStage stage);
  bool begin();
  void tickRing(uint32_t now);
  void tick();
  void onMessageFromWeb(const String& text);
  void setMode(Mode m);
  Mode getMode() const { return mode; }
  void setThreshold(uint8_t t);
  uint8_t getThreshold() const { return touchThreshold; }
  void setTimings(uint16_t onMs, uint16_t offMs, uint16_t gapMs);
  void setAnimation(const String& name, uint32_t rgb, uint16_t speedMs, uint8_t count = 0);
  void startCapture(char symbol);
  void cancelCapture();
  void saveCapture();
  void sendAlphabet();
  void playLetter(char symbol);
  void requestState();
  bool importGestures(JsonArray arr);
  void writeOutputs(uint16_t mask);
  void setDebugStreaming(bool enabled);
  bool debugStreamingEnabled() const { return debugStreaming; }

private:
  void shiftZero();
  void pulseOutput(uint8_t bit, uint16_t ms);
  void handleButton(uint32_t now);
  void handleSingleClick();
  void readInputs(uint32_t now);
  uint32_t scanInputs();
  uint8_t readAndWrite(uint32_t& bits, uint8_t index);
  void handleCapture(uint32_t now);
  void handleSymbol(char symbol);
  void vibrateError();
  void runSequencer(uint32_t now);
  void sendStatus(const char* reason);
  void sendMessageEvent(const char* dir, const String& txt);
  void sendTypingEvent(const char* state);
  void sendGestureState(bool expired = false);
  void publishInputs(uint32_t mask);
  void sendNoInput();
  void sendOutput(uint16_t mask);
  void sendCaptureEvent(const char* event);
  void push(JsonDocument& doc);
  void log(const char* msg);
  String normalize(const String& in);
  void pushSymbol(char c);
  void applyBaseRingState(uint32_t now);
  void showIdleRing();
  void showGestureProgress();
  void showModeIndicator(uint8_t modeIndex);

  GestureStore store;
  GesturePlayer player;
  LedRingAnimator ring;
  SendFn send;

  Mode mode = ModeChat;
  uint8_t touchThreshold = 10;
  uint8_t highInputs = 0;
  uint16_t lastOutputMask = 0;
  uint16_t timingOn = DEFAULT_STEP_ON_MS;
  uint16_t timingOff = DEFAULT_STEP_OFF_MS;
  uint16_t timingGap = DEFAULT_LETTER_GAP_MS;

  uint32_t lastRawMask = 0;
  uint32_t stableMask = 0;
  uint32_t lastNotifiedMask = 0;
  uint32_t lastMaskChange = 0;
  std::vector<uint16_t> currentSequence;
  uint32_t lastStepTime = 0;

  bool captureActive = false;
  char captureSymbol = 'a';
  uint32_t captureLastMask = 0;
  uint32_t captureStable = 0;
  uint32_t captureLastChange = 0;
  std::vector<uint16_t> captureSequence;

  bool lastButtonState = false;
  uint32_t lastButtonChange = 0;
  uint8_t clickCount = 0;
  uint32_t clickDeadline = 0;
  uint8_t playbackState = 0;

  uint8_t sequencerIndex = 0;
  uint32_t sequencerNext = 0;
  bool sequencerOn = false;

  uint32_t inputLockUntil = 0;
  uint32_t lastTouchFeedback = 0;
  uint32_t touchFeedbackOffAt = 0;

  uint32_t capacitanceValues[16] = {0};

  String typingBuffer;
  bool ringReady = false;
  uint32_t ringResumeAt = 0;
  bool idleRingActive = false;
  size_t lastSequenceVisualized = 0;
  bool debugStreaming = true;
  uint32_t lastInteraction = 0;
};
