#include <string.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs_flash.h"

#ifndef STASSID
#define STASSID "replace-me"
#endif
#ifndef STAPSK
#define STAPSK "replace-me"
#endif

static const char *TAG = "acme-ota";
static EventGroupHandle_t wifi_events;
static const EventBits_t WIFI_READY = BIT0;
static char device_id[13];
static char hostname[32];

static esp_err_t device_info(httpd_req_t *request)
{
    const esp_app_desc_t *app = esp_app_get_description();
    char version[sizeof(app->version) * 6 + 1];
    char *out = version;
    for (size_t i = 0; i < sizeof(app->version) && app->version[i]; ++i) {
        unsigned char character = app->version[i];
        if (character < 32) out += sprintf(out, "\\u%04x", (unsigned)character);
        else {
            if (character == '"' || character == '\\') *out++ = '\\';
            *out++ = character;
        }
    }
    *out = '\0';
    char body[384];
    snprintf(body, sizeof(body),
             "{\"acme\":1,\"ota_protocol\":\"http-v1\",\"id\":\"%s\","
             "\"hostname\":\"%s.local\",\"platform\":\"esp-idf\",\"version\":\"%s\"}",
             device_id, hostname, version);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, body);
}

static esp_err_t receive_body(httpd_req_t *request,
                              esp_err_t (*write_chunk)(const void *, size_t, void *),
                              void *context)
{
    char buffer[1024];
    int remaining = request->content_len;
    if (remaining <= 0) return ESP_ERR_INVALID_SIZE;
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
    if (!partition || request->content_len <= 0 || request->content_len > partition->size) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid firmware image size");
        return ESP_FAIL;
    }
    esp_ota_handle_t handle;
    esp_err_t begin = esp_ota_begin(partition, request->content_len, &handle);
    if (begin != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(begin));
        return begin;
    }
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
    if (partition == NULL || request->content_len <= 0 || request->content_len > partition->size) {
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
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(device_id, sizeof(device_id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(hostname, sizeof(hostname), "acme-idf-%s", device_id);
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
    const httpd_uri_t info = {
        .uri = "/info", .method = HTTP_GET, .handler = device_info,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &info));

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_ERROR_CHECK(mdns_instance_name_set("ACME ESP-IDF OTA"));
    mdns_txt_item_t txt[] = {
        {"acme", "1"}, {"ota_protocol", "http-v1"}, {"id", device_id},
    };
    ESP_ERROR_CHECK(mdns_service_add(hostname, "_arduino", "_tcp", 80, txt, 3));
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
    strlcpy((char *)config.sta.ssid, STASSID, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, STAPSK, sizeof(config.sta.password));
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
