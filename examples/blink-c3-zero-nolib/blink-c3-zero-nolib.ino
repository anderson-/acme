#include <Arduino.h>
#include "esp32-hal-rmt.h"

void writeRgbPixel(uint8_t pin, uint8_t red, uint8_t green, uint8_t blue, uint8_t address = 0) {
  static int initializedPin = -1;
  static bool rgbReady = false;
  if (initializedPin != pin) {
    rgbReady = rmtInit(pin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000UL);
    if (rgbReady) {
      rmtSetEOT(pin, LOW);
      initializedPin = pin;
    }
  }
  if (!rgbReady) return;

  rmt_data_t symbols[24];
  const rmt_data_t val[2] = {rmt_data_t{8, 1, 5, 0}, rmt_data_t{4, 1, 9, 0}};
  uint8_t symbolIndex = 0;
  for (uint8_t channel : {green, red, blue})
    for (uint8_t bitMask = 0x80; bitMask != 0; bitMask >>= 1)
      symbols[symbolIndex++] = val[!(channel & bitMask)];
  rmtWrite(pin, symbols, RMT_SYMBOLS_OF(symbols), RMT_WAIT_FOR_EVER);
}

#define LED_PIN        10
#define BLINK_INTERVAL 500

void setup() {
  writeRgbPixel(LED_PIN, 0, 0, 0);
}

void loop() {
  uint32_t startTime = millis();
  
  while (millis() - startTime < BLINK_INTERVAL) {
    uint8_t hue = map(millis() - startTime, 0, BLINK_INTERVAL, 0, 255);

    if (hue < 85) {
      writeRgbPixel(LED_PIN, 255 - (hue * 3), hue * 3, 0);
    } else if (hue < 170) {
      hue -= 85;
      writeRgbPixel(LED_PIN, 0, 255 - (hue * 3), hue * 3);
    } else {
      hue -= 170;
      writeRgbPixel(LED_PIN, hue * 3, 0, 255 - (hue * 3));
    }

    delay(10);
  }
  
  writeRgbPixel(LED_PIN, 0, 0, 0);
  delay(BLINK_INTERVAL);
}