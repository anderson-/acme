#include <FastLED.h>

#define LED_PIN        10
#define NUM_LEDS       1
#define BLINK_INTERVAL 500

CRGB leds[NUM_LEDS];

void setup() {
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS);
  FastLED.clear();
}

void loop() {
  uint32_t startTime = millis();
  
  while (millis() - startTime < BLINK_INTERVAL) {
    uint8_t hue = map(millis() - startTime, 0, BLINK_INTERVAL, 0, 255);
    leds[0] = CHSV(hue, 255, 50);
    FastLED.show();
    delay(1);
  }
  
  leds[0] = CRGB::Black;
  FastLED.show();
  delay(BLINK_INTERVAL);
}