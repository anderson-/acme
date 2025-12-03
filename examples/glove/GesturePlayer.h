#pragma once

#include <Arduino.h>
#include <vector>
#include "GloveConfig.h"
#include "GestureStore.h"

class GesturePlayer {
public:
  void setOutputWriter(const std::function<void(uint16_t)>& fn);
  void setTimings(uint16_t onMs, uint16_t offMs, uint16_t gapMs);
  void queueGesture(const std::vector<GestureStep>& steps);
  void queueMessage(const String& msg, const GestureStore& store);
  void restartLast(const GestureStore& store, bool slow);
  void clear();
  void tick(uint32_t now);
  bool isActive() const;
  bool isBlockingInputs() const;
  String lastMessageText() const;

private:
  struct Step {
    uint16_t mask;
    uint16_t on;
    uint16_t off;
  };
  enum State { Idle, Running, Pausing };
  void startNext();

  std::vector<Step> playback;
  std::function<void(uint16_t)> writeOutputs;
  State state = Idle;
  uint32_t nextChangeMs = 0;
  uint16_t stepOnMs = DEFAULT_STEP_ON_MS;
  uint16_t stepOffMs = DEFAULT_STEP_OFF_MS;
  uint16_t letterGapMs = DEFAULT_LETTER_GAP_MS;
  uint16_t pendingOffDelay = 0;
  uint16_t activeMask = 0;
  String lastMessage;
};
