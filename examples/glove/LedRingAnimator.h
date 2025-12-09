#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include "GloveConfig.h"

class LedRingAnimator {
public:
  enum Mode { Off, Solid, FadeIn, FadeOut, Ring, Blink, Pulse, Custom, Progress };

  void begin();
  void tick(uint32_t now);
  void off();
  void solid(const CRGB& color);
  void fadeTo(const CRGB& color, uint16_t ms, bool fadeIn);
  void ring(const CRGB& color, uint16_t stepMs);
  void blink(const CRGB& color, uint16_t onMs, uint16_t offMs, uint8_t times = 0);
  void pulse(const CRGB& color, uint16_t fadeInMs, uint16_t fadeOutMs, uint8_t times = 0);

  // Custom / per-led control
  void customClear(const CRGB& background = CRGB::Black);
  void setPixel(uint8_t index, const CRGB& color);
  void setPixelBlink(uint8_t index, const CRGB& color, uint16_t onMs, uint16_t offMs, uint8_t times = 0);
  void setPixelPulse(uint8_t index, const CRGB& color, uint16_t fadeInMs, uint16_t fadeOutMs, uint8_t times = 0);
  bool isCustom() const { return mode == Custom; }
  bool isRing() const { return mode == Ring; }
  bool isIdle() const { return mode == Off || mode == Ring; }
  bool isAnimating() const { return mode == Blink || mode == Pulse || mode == FadeIn || mode == FadeOut || mode == Progress; }
  Mode getMode() const { return mode; }

  // Progress bar (e.g., for OTA updates)
  void progress(uint8_t percent, const CRGB& color = CRGB(0, 200, 100));
  void progressPulse(uint8_t percent, const CRGB& color = CRGB(0, 200, 100));

private:
  enum PixelType { None, PixelSolid, PixelBlink, PixelPulse };
  struct PixelAnim {
    PixelType type = None;
    CRGB color = CRGB::Black;
    uint16_t onMs = 0;
    uint16_t offMs = 0;
    uint16_t fadeInMs = 0;
    uint16_t fadeOutMs = 0;
    uint8_t repeat = 0;
    uint8_t completed = 0;
    bool stateOn = false;
    bool pulseDirIn = true;
    uint32_t lastChange = 0;
    uint32_t startMs = 0;
  };

  void renderCustom(uint32_t now);
  void setAll(const CRGB& c);
  void renderFade(uint32_t now);
  void renderRing();
  void renderPulse(uint32_t now);
  void renderProgress(uint32_t now);
  float easeInOutCubic(float t);

  CRGB leds[RGB_CHAIN_LEN];
  Mode mode = Off;
  bool initialized = false;
  CRGB targetColor = CRGB::Blue;
  uint32_t startMs = 0;
  uint16_t durationMs = 0;
  uint16_t speedMs = 0;
  uint32_t lastStep = 0;
  uint8_t ringIndex = 0;
  uint16_t onDurationMs = 0;
  uint16_t offDurationMs = 0;
  uint8_t blinkRepeat = 0;
  uint8_t blinkCompleted = 0;
  bool blinkState = false;
  uint16_t fadeInMs = 0;
  uint16_t fadeOutMs = 0;
  bool pulseDirection = false;
  uint8_t pulseRepeat = 0;
  uint8_t pulseCompleted = 0;
  CRGB customBackground = CRGB::Black;
  PixelAnim pixels[RGB_CHAIN_LEN];

  // Progress state
  uint8_t progressPercent = 0;
  CRGB progressColor = CRGB(0, 200, 100);
  bool progressPulseEnabled = false;
};
