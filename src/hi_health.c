#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"

#include "hi_health.h"
#include "hi_notify.h"
#include "hi_system.h"

static const char *TAG = "HI_HEALTH";

#define NVS_NAMESPACE    "hi_health"
#define NVS_KEY_VERIFIED "verified_sha"
#define NVS_KEY_BOOTS    "boots"

static hi_health_config_t s_config;
static volatile bool s_pending_verify = false;
static char s_running_sha[17];
static uint8_t s_max_boots = 3;

bool hi_health_pending_verify(void)
{
    return s_pending_verify;
}

static void store(const char *verified_sha, uint8_t boots)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    if (verified_sha != NULL) nvs_set_str(h, NVS_KEY_VERIFIED, verified_sha);
    nvs_set_u8(h, NVS_KEY_BOOTS, boots);
    nvs_commit(h);
    nvs_close(h);
}

static void rollback_to_previous(const char *why)
{
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t desc;
    if (other == NULL || esp_ota_get_partition_description(other, &desc) != ESP_OK) {
        ESP_LOGE(TAG, "%s, but there is no valid previous image: staying on this one", why);
        store(NULL, 0);     // do not retry forever
        s_pending_verify = false;
        return;
    }
    ESP_LOGE(TAG, "%s: ROLLBACK to %s (%s %s)", why, other->label, desc.project_name, desc.version);
    store(NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(500));     // let the log reach UDP
    if (esp_ota_set_boot_partition(other) == ESP_OK) {
        esp_restart();
    }
    ESP_LOGE(TAG, "Setting the boot partition failed, staying on this image");
    s_pending_verify = false;
}

void hi_health_early_boot(uint8_t max_unverified_boots)
{
    if (max_unverified_boots) s_max_boots = max_unverified_boots;
    // Confirm to the bootloader first thing: resets from here on (e.g. brownouts) never roll back
    esp_ota_mark_app_valid_cancel_rollback();

    esp_err_t err = hi_nvs_init(NULL);
    esp_app_get_elf_sha256(s_running_sha, sizeof(s_running_sha));

    char verified[sizeof(s_running_sha)] = "";
    uint8_t boots = 0;
    nvs_handle_t h;
    if (err == ESP_OK && nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(verified);
        nvs_get_str(h, NVS_KEY_VERIFIED, verified, &len);
        nvs_get_u8(h, NVS_KEY_BOOTS, &boots);
        nvs_close(h);
    }
    if (strcmp(verified, s_running_sha) == 0) return;

    s_pending_verify = true;
    esp_reset_reason_t reason = esp_reset_reason();
    bool counts = reason != ESP_RST_BROWNOUT && reason != ESP_RST_POWERON;
    if (counts) boots++;
    ESP_LOGW(TAG, "(not error) Unverified firmware %s: boot %u of max %u (reset: %s%s)", s_running_sha, boots,
             s_max_boots, hi_reset_reason(), counts ? "" : ", not counted");
    store(NULL, boots);
    if (boots > s_max_boots) {
        rollback_to_previous("New firmware keeps resetting");
    }
}

static void health_task(void *arg)
{
    uint32_t verify_timeout_s = s_config.verify_timeout_s ? s_config.verify_timeout_s : 300;
    uint32_t stats_period_s = s_config.stats_period_s ? s_config.stats_period_s : 300;
    int64_t started_us = esp_timer_get_time();
    int64_t last_stats_us = started_us;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (s_config.tick) s_config.tick();

        int64_t now = esp_timer_get_time();
        if (s_pending_verify) {
            bool healthy = s_config.is_healthy == NULL || s_config.is_healthy();
            if (healthy) {
                store(s_running_sha, 0);
                s_pending_verify = false;
                ESP_LOGW(TAG, "(not error) New firmware %s verified after %lld s", s_running_sha,
                         (now - started_us) / 1000000);
            } else if (now - started_us > (int64_t) verify_timeout_s * 1000000) {
                char why[80];
                snprintf(why, sizeof(why), "New firmware unhealthy after %lu s", (unsigned long) verify_timeout_s);
                hi_notify_error(why);
                rollback_to_previous(why);
            }
        }
        if (s_config.log_stats && now - last_stats_us >= (int64_t) stats_period_s * 1000000) {
            last_stats_us = now;
            s_config.log_stats();
        }
    }
}

void hi_health_start(const hi_health_config_t *config)
{
    s_config = *config;
    if (hi_reset_was_unexpected()) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Unexpected reset: %s", hi_reset_reason());
        hi_notify_error(msg);
    }
    if (xTaskCreate(health_task, "hi_health", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create health task, rebooting");
        esp_restart();
    }
}
