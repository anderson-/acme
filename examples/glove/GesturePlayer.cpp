#include "GesturePlayer.h"

void GesturePlayer::setOutputWriter(const std::function<void(uint16_t)>& fn) {
  writeOutputs = fn;
}

void GesturePlayer::setTimings(uint16_t onMs, uint16_t offMs, uint16_t gapMs) {
  stepOnMs = onMs;
  stepOffMs = offMs;
  letterGapMs = gapMs;
}

void GesturePlayer::queueGesture(const std::vector<GestureStep>& steps) {
  for (const auto& s : steps) {
    playback.push_back({s.mask, s.holdMs ? s.holdMs : stepOnMs, s.pauseMs ? s.pauseMs : stepOffMs});
  }
  if (state == Idle) startNext();
}

void GesturePlayer::queueMessage(const String& msg, const GestureStore& store) {
  lastMessage = msg;
  for (size_t i = 0; i < msg.length(); i++) {
    char c = msg[i];
    const auto* g = store.get(c);
    if (!g) continue;
    if (!playback.empty()) playback.push_back({0, 0, letterGapMs});
    for (const auto& s : *g) playback.push_back({s.mask, s.holdMs ? s.holdMs : stepOnMs, s.pauseMs ? s.pauseMs : stepOffMs});
  }
  if (state == Idle) startNext();
}

void GesturePlayer::restartLast(const GestureStore& store, bool slow) {
  if (lastMessage.isEmpty()) return;
  clear();
  if (slow) setTimings(stepOnMs * 2, stepOffMs * 2, letterGapMs * 2);
  queueMessage(lastMessage, store);
}

void GesturePlayer::clear() {
  playback.clear();
  state = Idle;
  if (writeOutputs) writeOutputs(0);
}

void GesturePlayer::tick(uint32_t now) {
  if (state == Idle && !playback.empty()) {
    state = Pausing;
    nextChangeMs = now;
  }
  if (state == Running && now >= nextChangeMs) {
    if (writeOutputs) writeOutputs(0);
    state = Pausing;
    nextChangeMs = now + pendingOffDelay;
    activeMask = 0;
    return;
  }
  if (state == Pausing && now >= nextChangeMs) {
    if (playback.empty()) { state = Idle; return; }
    Step s = playback.front();
    playback.erase(playback.begin());
    if (s.mask == 0 && s.on == 0) { nextChangeMs = now + s.off; return; }
    activeMask = s.mask;
    if (writeOutputs) writeOutputs(activeMask);
    pendingOffDelay = s.off;
    nextChangeMs = now + s.on;
    state = Running;
  }
}

bool GesturePlayer::isActive() const { return state != Idle; }
bool GesturePlayer::isBlockingInputs() const { return state == Running; }
String GesturePlayer::lastMessageText() const { return lastMessage; }

void GesturePlayer::startNext() {
  nextChangeMs = millis();
  state = Pausing;
}
