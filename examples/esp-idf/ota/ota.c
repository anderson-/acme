#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs_flash.h"

#ifndef WIFI_SSID
#define WIFI_SSID "replace-me"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "replace-me"
#endif

static const char *TAG = "acme-ota";
static EventGroupHandle_t wifi_events;
static const EventBits_t WIFI_READY = BIT0;

static esp_err_t receive_body(httpd_req_t *request,
                              esp_err_t (*write_chunk)(const void *, size_t, void *),
                              void *context)
{
    char buffer[1024];
    int remaining = request->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(request, buffer,
                                      remaining < sizeof(buffer) ? remaining : sizeof(buffer));
        if (received <= 0) {
            return ESP_FAIL;
        }
        ESP_RETURN_ON_ERROR(write_chunk(buffer, received, context), TAG, "write failed");
        remaining -= received;
    }
    return ESP_OK;
}

static esp_err_t ota_write(const void *data, size_t size, void *context)
{
    return esp_ota_write(*(esp_ota_handle_t *)context, data, size);
}

static esp_err_t firmware_update(httpd_req_t *request)
{
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t handle;
    ESP_RETURN_ON_ERROR(esp_ota_begin(partition, request->content_len, &handle), TAG,
                        "OTA begin failed");
    esp_err_t result = receive_body(request, ota_write, &handle);
    if (result == ESP_OK) result = esp_ota_end(handle);
    else esp_ota_abort(handle);
    if (result == ESP_OK) result = esp_ota_set_boot_partition(partition);
    if (result != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(result));
        return result;
    }
    httpd_resp_sendstr(request, "firmware accepted; rebooting\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

typedef struct {
    const esp_partition_t *partition;
    size_t offset;
} fs_write_context_t;

static esp_err_t filesystem_write(const void *data, size_t size, void *opaque)
{
    fs_write_context_t *context = opaque;
    esp_err_t result = esp_partition_write(context->partition, context->offset, data, size);
    context->offset += size;
    return result;
}

static esp_err_t filesystem_update(httpd_req_t *request)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "storage");
    if (partition == NULL || request->content_len > partition->size) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid filesystem image");
        return ESP_FAIL;
    }
    ESP_RETURN_ON_ERROR(esp_partition_erase_range(partition, 0, partition->size), TAG,
                        "filesystem erase failed");
    fs_write_context_t context = {.partition = partition, .offset = 0};
    esp_err_t result = receive_body(request, filesystem_write, &context);
    if (result != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(result));
        return result;
    }
    httpd_resp_sendstr(request, "filesystem accepted; rebooting\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

static void start_update_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    const httpd_uri_t firmware = {
        .uri = "/update", .method = HTTP_POST, .handler = firmware_update,
    };
    const httpd_uri_t filesystem = {
        .uri = "/update-fs", .method = HTTP_POST, .handler = filesystem_update,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &firmware));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &filesystem));

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("acme-idf"));
    ESP_ERROR_CHECK(mdns_instance_name_set("ACME ESP-IDF OTA"));
    ESP_ERROR_CHECK(mdns_service_add("ACME ESP-IDF OTA", "_arduino", "_tcp", 80, NULL, 0));
    ESP_LOGI(TAG, "OTA ready: POST /update or /update-fs");
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_READY);
    }
}

static void connect_wifi(void)
{
    wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, WIFI_SSID, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, WIFI_PASSWORD, sizeof(config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(wifi_events, WIFI_READY, pdFALSE, pdTRUE, portMAX_DELAY);
}

void app_main(void)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);
    connect_wifi();
    start_update_server();
}
