#pragma once

#include <Arduino.h>

enum class SwioBitResult : uint8_t {
  Zero,
  One,
  Timeout,
};

bool swioInit();

// CNLohr ESP32-S2 startup state: actively drive SWIO high for 5 ms.
void swioSynchronize();

// A non-critical diagnostic measurement of the exact upstream delay loop.
uint32_t swioMeasureUpstreamT1Cycles();

void IRAM_ATTR swioDriveLow();
void IRAM_ATTR swioDriveHigh();
void IRAM_ATTR swioRelease();
void IRAM_ATTR swioWriteBit(bool value);
SwioBitResult IRAM_ATTR swioReadBit();
