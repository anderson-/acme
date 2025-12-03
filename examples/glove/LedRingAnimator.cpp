#include "LedRingAnimator.h"

void LedRingAnimator::begin() {
  FastLED.addLeds<NEOPIXEL, PIN_RGB>(leds, RGB_CHAIN_LEN);
  FastLED.clear(true);
}

void LedRingAnimator::tick(uint32_t now) {
  switch (mode) {
    case Solid:
      FastLED.show();
      break;
    case FadeIn:
    case FadeOut:
      if (durationMs == 0) return;
      if (now - startMs >= durationMs) {
        if (mode == FadeOut) {
          mode = Off;
          setAll(CRGB::Black);
        } else {
          mode = Solid;
          setAll(targetColor);
        }
        FastLED.show();
        return;
      }
      renderFade(now);
      break;
    case Blink:
      if (onDurationMs == 0 || offDurationMs == 0) return;
      if (now - lastStep >= (blinkState ? onDurationMs : offDurationMs)) {
        lastStep = now;
        blinkState = !blinkState;
        setAll(blinkState ? targetColor : CRGB::Black);
        FastLED.show();
      }
      break;
    case Pulse: {
      if (fadeInMs == 0 || fadeOutMs == 0) return;
      uint16_t currentDuration = pulseDirection ? fadeInMs : fadeOutMs;
      if (now - startMs >= currentDuration) {
        startMs = now;
        pulseDirection = !pulseDirection;
      }
      renderPulse(now);
      break;
    }
    case Ring:
      if (speedMs == 0) return;
      if (now - lastStep >= speedMs) {
        lastStep = now;
        ringIndex = (ringIndex + 1) % RGB_CHAIN_LEN;
        renderRing();
      }
      break;
    case Off:
    default:
      break;
  }
}

void LedRingAnimator::off() {
  mode = Off;
  setAll(CRGB::Black);
  FastLED.show();
}

void LedRingAnimator::solid(const CRGB& color) {
  mode = Solid;
  targetColor = color;
  setAll(color);
  FastLED.show();
}

void LedRingAnimator::fadeTo(const CRGB& color, uint16_t ms, bool fadeIn) {
  mode = fadeIn ? FadeIn : FadeOut;
  targetColor = color;
  startMs = millis();
  durationMs = ms;
  if (fadeIn) {
    setAll(CRGB::Black);
  } else {
    setAll(color);
  }
  FastLED.show();
}

void LedRingAnimator::ring(const CRGB& color, uint16_t stepMs) {
  mode = Ring;
  targetColor = color;
  ringIndex = 0;
  speedMs = stepMs;
  lastStep = millis();
  renderRing();
}

void LedRingAnimator::blink(const CRGB& color, uint16_t onMs, uint16_t offMs) {
  mode = Blink;
  targetColor = color;
  onDurationMs = onMs;
  offDurationMs = offMs;
  blinkState = true;
  lastStep = millis();
  setAll(color);
  FastLED.show();
}

void LedRingAnimator::pulse(const CRGB& color, uint16_t fadeInMs, uint16_t fadeOutMs) {
  mode = Pulse;
  targetColor = color;
  this->fadeInMs = fadeInMs;
  this->fadeOutMs = fadeOutMs;
  pulseDirection = true;
  startMs = millis();
}

void LedRingAnimator::setAll(const CRGB& c) {
  for (auto& led : leds) led = c;
}

void LedRingAnimator::renderFade(uint32_t now) {
  float progress = float(now - startMs) / float(durationMs);
  progress = constrain(progress, 0.0f, 1.0f);
  progress = easeInOutCubic(progress);

  float scale = progress;
  if (mode == FadeOut) scale = 1.0f - progress;

  CRGB current = targetColor;
  current.nscale8_video(uint8_t(scale * 255));
  setAll(current);
  FastLED.show();
}

void LedRingAnimator::renderPulse(uint32_t now) {
  uint16_t currentDuration = pulseDirection ? fadeInMs : fadeOutMs;
  float progress = float(now - startMs) / float(currentDuration);
  progress = constrain(progress, 0.0f, 1.0f);
  progress = easeInOutCubic(progress);

  float scale = pulseDirection ? progress : 1.0f - progress;

  CRGB current = targetColor;
  current.nscale8_video(uint8_t(scale * 255));
  setAll(current);
  FastLED.show();
}

float LedRingAnimator::easeInOutCubic(float t) {
  return t < 0.5f ? 4.0f * t * t * t : 1.0f - pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

void LedRingAnimator::renderRing() {
  for (int i = 0; i < RGB_CHAIN_LEN; i++) {
    leds[i] = (i == ringIndex) ? targetColor : CRGB::Black;
  }
  FastLED.show();
}
