#pragma once
#include "Arduino.h"
#include <vector>
#define U_FLASH 0
#define U_SPIFFS 100
#define U_FS 100
inline struct {
  bool begun = false, failWrite = false;
  int command = -1;
  size_t expected = 0, largestChunk = 0;
  std::vector<uint8_t> data;
  bool begin(size_t size, int cmd) { begun = true; expected = size; command = cmd; data.clear(); return true; }
  size_t write(uint8_t *buffer, size_t size) {
    largestChunk = std::max(largestChunk, size);
    if (failWrite) return 0;
    data.insert(data.end(), buffer, buffer+size); return size;
  }
  bool end() { bool complete = data.size() == expected; begun = false; return complete; }
#if !defined(ESP8266)
  void abort() { begun = false; }
#endif
} Update;
