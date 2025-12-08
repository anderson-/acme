#include "LedRingAnimator.h"

void LedRingAnimator::begin() {
  if (!initialized) {
    FastLED.addLeds<NEOPIXEL, PIN_RGB>(leds, RGB_CHAIN_LEN);
    initialized = true;
  }
  FastLED.clear(true);
  mode = Off;
}

void LedRingAnimator::tick(uint32_t now) {
  if (!initialized) return;
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
        if (blinkState && blinkRepeat > 0) {
          blinkCompleted++;
          if (blinkCompleted >= blinkRepeat) {
            mode = Off;
            setAll(CRGB::Black);
            FastLED.show();
            return;
          }
        }
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
        if (!pulseDirection && pulseRepeat > 0) {
          pulseCompleted++;
          if (pulseCompleted >= pulseRepeat) {
            mode = Off;
            setAll(CRGB::Black);
            FastLED.show();
            return;
          }
        }
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
    case Custom:
      renderCustom(now);
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

void LedRingAnimator::blink(const CRGB& color, uint16_t onMs, uint16_t offMs, uint8_t times) {
  mode = Blink;
  targetColor = color;
  onDurationMs = onMs;
  offDurationMs = offMs;
  blinkRepeat = times;
  blinkCompleted = 0;
  blinkState = true;
  lastStep = millis();
  setAll(color);
  FastLED.show();
}

void LedRingAnimator::pulse(const CRGB& color, uint16_t fadeInMs, uint16_t fadeOutMs, uint8_t times) {
  mode = Pulse;
  targetColor = color;
  this->fadeInMs = fadeInMs;
  this->fadeOutMs = fadeOutMs;
  pulseRepeat = times;
  pulseCompleted = 0;
  pulseDirection = true;
  startMs = millis();
}

void LedRingAnimator::customClear(const CRGB& background) {
  mode = Custom;
  customBackground = background;
  for (auto& p : pixels) p = PixelAnim{};
  setAll(background);
  FastLED.show();
}

void LedRingAnimator::setPixel(uint8_t index, const CRGB& color) {
  if (index >= RGB_CHAIN_LEN) return;
  mode = Custom;
  pixels[index] = PixelAnim{};
  pixels[index].type = PixelSolid;
  pixels[index].color = color;
  leds[index] = color;
  FastLED.show();
}

void LedRingAnimator::setPixelBlink(uint8_t index, const CRGB& color, uint16_t onMs, uint16_t offMs, uint8_t times) {
  if (index >= RGB_CHAIN_LEN) return;
  mode = Custom;
  PixelAnim anim;
  anim.type = PixelBlink;
  anim.color = color;
  anim.onMs = onMs;
  anim.offMs = offMs;
  anim.repeat = times;
  anim.completed = 0;
  anim.stateOn = true;
  anim.lastChange = millis();
  pixels[index] = anim;
  leds[index] = color;
  FastLED.show();
}

void LedRingAnimator::setPixelPulse(uint8_t index, const CRGB& color, uint16_t fadeIn, uint16_t fadeOut, uint8_t times) {
  if (index >= RGB_CHAIN_LEN) return;
  mode = Custom;
  PixelAnim anim;
  anim.type = PixelPulse;
  anim.color = color;
  anim.fadeInMs = fadeIn;
  anim.fadeOutMs = fadeOut;
  anim.repeat = times;
  anim.completed = 0;
  anim.pulseDirIn = true;
  anim.startMs = millis();
  pixels[index] = anim;
  leds[index] = customBackground;
  FastLED.show();
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

void LedRingAnimator::renderCustom(uint32_t now) {
  bool dirty = false;
  for (uint8_t i = 0; i < RGB_CHAIN_LEN; i++) {
    PixelAnim& p = pixels[i];
    CRGB current = customBackground;
    switch (p.type) {
      case PixelSolid:
        current = p.color;
        break;
      case PixelBlink: {
        if (p.onMs == 0 || p.offMs == 0) { current = p.stateOn ? p.color : customBackground; break; }
        uint16_t duration = p.stateOn ? p.onMs : p.offMs;
        if (now - p.lastChange >= duration) {
          p.lastChange = now;
          if (p.stateOn && p.repeat > 0) {
            p.completed++;
            if (p.completed >= p.repeat) {
              p.type = None;
              current = customBackground;
              p.stateOn = false;
              break;
            }
          }
          p.stateOn = !p.stateOn;
        }
        current = p.stateOn ? p.color : customBackground;
        break;
      }
      case PixelPulse: {
        if (p.fadeInMs == 0 || p.fadeOutMs == 0) { current = p.color; break; }
        uint16_t duration = p.pulseDirIn ? p.fadeInMs : p.fadeOutMs;
        float progress = float(now - p.startMs) / float(duration);
        if (progress >= 1.0f) {
          p.startMs = now;
          if (!p.pulseDirIn && p.repeat > 0) {
            p.completed++;
            if (p.completed >= p.repeat) {
              p.type = None;
              current = customBackground;
              break;
            }
          }
          p.pulseDirIn = !p.pulseDirIn;
          duration = p.pulseDirIn ? p.fadeInMs : p.fadeOutMs;
          progress = 0.0f;
        }
        progress = constrain(progress, 0.0f, 1.0f);
        progress = easeInOutCubic(progress);
        float scale = p.pulseDirIn ? progress : 1.0f - progress;
        current = p.color;
        current.nscale8_video(uint8_t(scale * 255));
        break;
      }
      case None:
      default:
        current = customBackground;
        break;
    }
    if (leds[i] != current) {
      leds[i] = current;
      dirty = true;
    }
  }
  if (dirty) FastLED.show();
}
