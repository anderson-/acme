#include "GestureStore.h"
#include <SPIFFS.h>

bool GestureStore::begin() {
  if (!SPIFFS.begin(true)) return false;
  if (!load()) {
    buildDefaults();
    save();
  }
  return true;
}

bool GestureStore::load() {
  File f = SPIFFS.open(kFile, "r");
  if (!f) return false;
  DynamicJsonDocument doc(6144);
  if (deserializeJson(doc, f)) {
    f.close();
    return false;
  }
  f.close();
  gestures.clear();
  for (JsonPair kv : doc.as<JsonObject>()) {
    String symbol = kv.key().c_str();
    if (symbol.length() != 1) continue;

    JsonArray maskArray = kv.value().as<JsonArray>();
    if (maskArray.isNull()) continue;

    Gesture gesture;
    gesture.symbol = symbol[0];

    for (JsonVariant maskValue : maskArray) {
      GestureStep s;
      s.mask = maskValue | 0;
      s.holdMs = DEFAULT_STEP_ON_MS;
      s.pauseMs = DEFAULT_STEP_OFF_MS;
      gesture.steps.push_back(s);
    }

    if (!gesture.steps.empty()) gestures.push_back(gesture);
  }
  if (gestures.empty()) {
    buildDefaults();
    return false;
  }
  return true;
}

bool GestureStore::save() {
  DynamicJsonDocument doc(6144);
  JsonObject root = doc.to<JsonObject>();
  for (const auto& g : gestures) {
    JsonArray maskArray = root.createNestedArray(String(g.symbol));
    for (const auto& s : g.steps) {
      maskArray.add(s.mask);
    }
  }
  File f = SPIFFS.open(kFile, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

bool GestureStore::updateGesture(char symbol, const std::vector<GestureStep>& steps) {
  for (auto& g : gestures) {
    if (g.symbol == symbol) {
      g.steps = steps;
      return save();
    }
  }
  Gesture g;
  g.symbol = symbol;
  g.steps = steps;
  gestures.push_back(g);
  return save();
}

const std::vector<GestureStep>* GestureStore::get(char symbol) const {
  for (const auto& g : gestures) if (g.symbol == symbol) return &g.steps;
  return nullptr;
}

std::vector<char> GestureStore::symbols() const {
  std::vector<char> s;
  for (const auto& g : gestures) s.push_back(g.symbol);
  return s;
}

void GestureStore::asJson(JsonDocument& doc) const {
  JsonObject root = doc.to<JsonObject>();
  for (const auto& g : gestures) {
    JsonArray maskArray = root.createNestedArray(String(g.symbol));
    for (const auto& s : g.steps) {
      maskArray.add(s.mask);
    }
  }
}

std::vector<char> GestureStore::prefixMatches(const std::vector<uint16_t>& sequence) const {
  std::vector<char> matches;
  for (const auto& g : gestures) {
    if (sequence.size() > g.steps.size()) continue;
    bool ok = true;
    for (size_t i = 0; i < sequence.size(); i++) {
      if (g.steps[i].mask != sequence[i]) { ok = false; break; }
    }
    if (ok) matches.push_back(g.symbol);
  }
  return matches;
}

char GestureStore::fullMatch(const std::vector<uint16_t>& sequence) const {
  for (const auto& g : gestures) {
    if (sequence.size() != g.steps.size()) continue;
    bool ok = true;
    for (size_t i = 0; i < sequence.size(); i++) {
      if (g.steps[i].mask != sequence[i]) { ok = false; break; }
    }
    if (ok) return g.symbol;
  }
  return '\0';
}

bool GestureStore::importGestures(JsonObject obj) {
  if (!obj) return false;
  for (JsonPair kv : obj) {
    String symbol = kv.key().c_str();
    if (symbol.length() != 1) continue;

    JsonArray maskArray = kv.value().as<JsonArray>();
    if (!maskArray) continue;

    std::vector<GestureStep> steps;
    for (JsonVariant maskValue : maskArray) {
      GestureStep s;
      s.mask = maskValue | 0;
      s.holdMs = DEFAULT_STEP_ON_MS;
      s.pauseMs = DEFAULT_STEP_OFF_MS;
      steps.push_back(s);
    }

    if (!steps.empty()) updateGesture(symbol[0], steps);
  }
  return true;
}

bool GestureStore::resetToDefaults() {
  gestures.clear();
  buildDefaults();
  return save();
}

void GestureStore::buildDefaults() {
  gestures.clear();
  auto addSingle = [&](char c, uint16_t mask) {
    Gesture g;
    g.symbol = c;
    g.steps.push_back(GestureStep{mask, DEFAULT_STEP_ON_MS, DEFAULT_STEP_OFF_MS});
    gestures.push_back(g);
  };
  addSingle('a', 1 << 0);
  addSingle('b', 1 << 1);
  addSingle('c', 1 << 2);
  addSingle('d', 1 << 3);
  addSingle('e', 1 << 4);
  addSingle('f', 1 << 5);
  addSingle('g', 1 << 6);
  addSingle('h', 1 << 7);
  addSingle('i', 1 << 8);
  addSingle('j', 1 << 9);
  addSingle('k', 1 << 10);
  addSingle('l', 1 << 11);
  addSingle('m', 1 << 12);
  addSingle('n', 1 << 13);
  addSingle('o', 1 << 14);
  addSingle('p', 1 << 15);
  addSingle('q', (1 << 0) | (1 << 8));
  addSingle('r', (1 << 1) | (1 << 9));
  addSingle('s', (1 << 2) | (1 << 10));
  addSingle('t', (1 << 3) | (1 << 11));
  addSingle('u', (1 << 4) | (1 << 12));
  addSingle('v', (1 << 5) | (1 << 13));
  addSingle('w', (1 << 6) | (1 << 7));
  addSingle('x', (1 << 6) | (1 << 14));
  addSingle('y', (1 << 7) | (1 << 15));
  addSingle('z', (1 << 0) | (1 << 7));
  addSingle('W', (1 << 14) | (1 << 15));
}
