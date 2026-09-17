#include "esp_chip_info.h"
#include "esp_log.h"

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI("acme", "Hello from %d-core ESP32 target", chip.cores);
}
