#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"

#include "hi_notify.h"
#include "hi_ota.h"

static const char *TAG = "HI_OTA";

static hi_ota_config_t s_config;
static volatile bool s_running = false;

void hi_ota_init(const hi_ota_config_t *config)
{
    s_config = *config;
}

static esp_err_t delete_bin(void)
{
    esp_http_client_config_t http = {
        .url = s_config.url,
        .cert_pem = s_config.cert_pem,
        .method = HTTP_METHOD_DELETE,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http);
    if (client == NULL) return ESP_FAIL;
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t hi_ota_run(void)
{
    if (s_config.url == NULL) return ESP_ERR_INVALID_STATE;

    esp_http_client_config_t http = {
        .url = s_config.url,
        .cert_pem = s_config.cert_pem,
        .keep_alive_enable = true,
        .timeout_ms = 10000,
    };
    esp_https_ota_config_t ota = {.http_config = &http};
    ESP_LOGI(TAG, "Checking %s", s_config.url);

    // A missing image on the server is the normal case: do not turn it into an error notification
    hi_notify_error_suppress(true);
    esp_err_t err = esp_https_ota(&ota);
    hi_notify_error_suppress(false);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No update installed: %s", esp_err_to_name(err));
        return err;
    }
    if (s_config.delete_after && delete_bin() != ESP_OK) {
        ESP_LOGW(TAG, "Firmware installed, but deleting it from the server failed");
    }
    ESP_LOGW(TAG, "(not error) Firmware installed, restarting");
    vTaskDelay(pdMS_TO_TICKS(300));     // let the log reach UDP
    esp_restart();
    return ESP_OK;
}

static void ota_task(void *arg)
{
    hi_ota_run();
    s_running = false;
    vTaskDelete(NULL);
}

bool hi_ota_start_background(void)
{
    if (s_running || s_config.url == NULL) return false;
    s_running = true;
    if (xTaskCreate(ota_task, "hi_ota", CONFIG_HOME_IDF_OTA_TASK_STACK, NULL, 4, NULL) != pdPASS) {
        s_running = false;
        ESP_LOGE(TAG, "Failed to create OTA task");
        return false;
    }
    return true;
}

bool hi_ota_is_running(void)
{
    return s_running;
}
