#pragma once
#include "Arduino.h"
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <cstring>

struct ClientState { std::string input, output; size_t offset = 0; bool closed = false; };
inline std::shared_ptr<ClientState> incoming;
class WiFiClient {
 public:
  std::shared_ptr<ClientState> state;
  explicit WiFiClient(std::shared_ptr<ClientState> s = nullptr) : state(s) {}
  explicit operator bool() const { return state != nullptr; }
  void setTimeout(int) {}
  bool connected() const { return !state->closed && state->offset < state->input.size(); }
  int available() const { return state->closed ? 0 : state->input.size()-state->offset; }
  String readStringUntil(char delimiter) {
    auto end = state->input.find(delimiter, state->offset);
    if (end == std::string::npos) end = state->input.size();
    String result(state->input.substr(state->offset, end-state->offset));
    state->offset = std::min(end+1, state->input.size());
    return result;
  }
  int read(uint8_t *buffer, size_t size) {
    size = std::min(size, static_cast<size_t>(available()));
    memcpy(buffer, state->input.data()+state->offset, size);
    state->offset += size;
    return size;
  }
  void printf(const char *format, ...) {
    char buffer[4096]; va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); state->output += buffer;
  }
  void print(const String &s) { state->output += s.value; }
  void stop() { state->closed = true; }
};
class WiFiServer {
 public:
  explicit WiFiServer(uint16_t) {}
  void begin() {}
  WiFiClient available() { return WiFiClient(incoming); }
};
inline struct { String macAddress() { return "AA:BB:CC:DD:EE:FF"; } } WiFi;
