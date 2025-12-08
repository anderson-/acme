#pragma once

#include <Arduino.h>

// Hardware map (mirrors glove)
static const uint8_t PIN_Z0 = 4;
static const uint8_t PIN_Z1 = 13;
static const uint8_t PIN_S0 = 16;
static const uint8_t PIN_S1 = 17;
static const uint8_t PIN_S2 = 22;
static const uint8_t PIN_LATCH = 19;
static const uint8_t PIN_DATA = 23;
static const uint8_t PIN_CLOCK = 18;
static const uint8_t PIN_LED = 5;
static const uint8_t PIN_MOSFET = 33;
static const uint8_t PIN_BUTTON = 21;
static const uint8_t PIN_RGB = 32;
static const uint8_t RGB_CHAIN_LEN = 5;

// Shift and mux timing
static const uint16_t LATCH_DELAY_US = 40;
static const uint16_t SOFT_SHIFT_DELAY_US = 10;
static const uint16_t MUX_DELAY_US = 10;
static const uint16_t CAPACITANCE_DELAY_US = 5;

// Gesture timings
static const uint16_t GESTURE_STABLE_MS = 500;
static const uint16_t GESTURE_TIMEOUT_MS = 2500;
static const uint16_t DEFAULT_STEP_ON_MS = 100;
static const uint16_t DEFAULT_STEP_OFF_MS = 120;
static const uint16_t DEFAULT_LETTER_GAP_MS = 200;

// Sequencer timings
static const uint16_t SEQ_ON_MS = 50;
static const uint16_t SEQ_OFF_MS = 200;
