#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "GloveConfig.h"

struct GestureStep {
  uint16_t mask = 0;
  uint16_t holdMs = DEFAULT_STEP_ON_MS;
  uint16_t pauseMs = DEFAULT_STEP_OFF_MS;
};

struct Gesture {
  char symbol;
  std::vector<GestureStep> steps;
};

class GestureStore {
public:
  bool begin();
  bool load();
  bool save();
  bool updateGesture(char symbol, const std::vector<GestureStep>& steps);
  const std::vector<GestureStep>* get(char symbol) const;
  std::vector<char> symbols() const;
  void asJson(JsonDocument& doc) const;
  std::vector<char> prefixMatches(const std::vector<uint16_t>& sequence) const;
  std::vector<char> exactLengthMatches(const std::vector<uint16_t>& sequence) const;
  char fullMatch(const std::vector<uint16_t>& sequence) const;
  bool importGestures(JsonObject obj);
  bool resetToDefaults();

private:
  void buildDefaults();

  std::vector<Gesture> gestures;
  const char* kFile = "/gestures.json";
};
