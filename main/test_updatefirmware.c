#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_crt_bundle.h"

#define WIFI_SSID      "SURONA COFFEE-T2"
#define WIFI_PASS      "suronacoffee"

#define VERSION_URL    "https://raw.githubusercontent.com/nhatminhhhhhh/test_OTA/main/version.txt"
#define OTA_URL        "https://raw.githubusercontent.com/nhatminhhhhhh/test_OTA/main/build/test_updatefirmware.bin"

#define CURRENT_FW_VERSION "1.0.4"
static const char *TAG = "update_firmware";
static bool wifi_connected = false;


/* ================= WIFI ================= */

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        wifi_connected = false;
        esp_wifi_connect();
    }
}

static void ip_event_handler(void *arg,
                            esp_event_base_t event_base,
                            int32_t event_id,
                            void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
    }
}

static void wifi_init_sta(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT,
                               ESP_EVENT_ANY_ID,
                               &wifi_event_handler,
                               NULL);
    esp_event_handler_register(IP_EVENT,
                               IP_EVENT_STA_GOT_IP,
                               &ip_event_handler,
                               NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

/* ================= VERSION CHECK ================= */

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    return ESP_OK;
}

static bool check_new_version(void)
{
    char buffer[32] = {0};

    esp_http_client_config_t config = {
        .url = VERSION_URL,
        .event_handler = http_event_handler,
        .skip_cert_common_name_check = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_perform(client);

    int len = esp_http_client_read(client, buffer, sizeof(buffer) - 1);
    esp_http_client_cleanup(client);

    if (len <= 0) return false;

    ESP_LOGI(TAG, "Current: %s | Server: %s",
             CURRENT_FW_VERSION, buffer);

    return (strcmp(buffer, CURRENT_FW_VERSION) != 0);
}

/* ================= OTA TASK ================= */

static void ota_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Checking firmware version...");

    if (!check_new_version()) {
        ESP_LOGI(TAG, "Firmware is up to date");
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "New firmware found, starting OTA...");

    esp_http_client_config_t http_config = {
        .url = OTA_URL,
        .timeout_ms = 10000,
        .skip_cert_common_name_check = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA success, restarting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed");
    }

    vTaskDelete(NULL);
}

/* ================= MAIN ================= */

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();

    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    int retry = 0;
    while (!wifi_connected && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    if (!wifi_connected) {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        return;
    }

    ESP_LOGI(TAG, "WiFi connected successfully");

    xTaskCreate(ota_task, "ota_task", 8192, NULL, 5, NULL);

    while (1) {
        ESP_LOGI(TAG, "Current firmware version: %s", CURRENT_FW_VERSION);
        ESP_LOGI(TAG, "Running application...");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
