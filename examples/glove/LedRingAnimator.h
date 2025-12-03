#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include "GloveConfig.h"

class LedRingAnimator {
public:
  enum Mode { Off, Solid, FadeIn, FadeOut, Ring, Blink, Pulse };

  void begin();
  void tick(uint32_t now);
  void off();
  void solid(const CRGB& color);
  void fadeTo(const CRGB& color, uint16_t ms, bool fadeIn);
  void ring(const CRGB& color, uint16_t stepMs);
  void blink(const CRGB& color, uint16_t onMs, uint16_t offMs);
  void pulse(const CRGB& color, uint16_t fadeInMs, uint16_t fadeOutMs);

private:
  void setAll(const CRGB& c);
  void renderFade(uint32_t now);
  void renderRing();
  void renderPulse(uint32_t now);
  float easeInOutCubic(float t);

  CRGB leds[RGB_CHAIN_LEN];
  Mode mode = Off;
  CRGB targetColor = CRGB::Blue;
  uint32_t startMs = 0;
  uint16_t durationMs = 0;
  uint16_t speedMs = 0;
  uint32_t lastStep = 0;
  uint8_t ringIndex = 0;
  uint16_t onDurationMs = 0;
  uint16_t offDurationMs = 0;
  bool blinkState = false;
  uint16_t fadeInMs = 0;
  uint16_t fadeOutMs = 0;
  bool pulseDirection = false;
};
