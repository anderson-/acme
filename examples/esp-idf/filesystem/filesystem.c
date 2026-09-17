#include <stdio.h>
#include "esp_log.h"
#include "esp_spiffs.h"

void app_main(void)
{
    esp_vfs_spiffs_conf_t config = {
        .base_path = "/storage",
        .partition_label = "storage",
        .max_files = 4,
        .format_if_mount_failed = false,
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&config));

    FILE *file = fopen("/storage/message.txt", "r");
    if (file == NULL) {
        ESP_LOGE("acme", "message.txt not found");
        return;
    }
    char message[96];
    if (fgets(message, sizeof(message), file) != NULL) {
        ESP_LOGI("acme", "%s", message);
    }
    fclose(file);
}
