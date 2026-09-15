#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "hi_system.h"

static const char *TAG = "HI_SYS";

esp_err_t hi_nvs_init(bool *erased)
{
    static bool s_done = false;
    if (erased) *erased = false;
    if (s_done) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS unusable (%s): erasing, stored state is lost", esp_err_to_name(err));
        if (erased) *erased = true;
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err == ESP_OK) s_done = true;
    return err;
}

const char *hi_reset_reason(void)
{
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "external";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

bool hi_reset_was_unexpected(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT
           || r == ESP_RST_BROWNOUT;
}

static void restart_cb(void *arg)
{
    esp_restart();
}

void hi_restart_soon(uint32_t delay_ms)
{
    static esp_timer_handle_t timer = NULL;
    const esp_timer_create_args_t args = {.callback = restart_cb, .name = "hi_restart"};
    if (timer == NULL && esp_timer_create(&args, &timer) != ESP_OK) {
        esp_restart();
    }
    esp_timer_stop(timer);
    esp_timer_start_once(timer, (uint64_t) delay_ms * 1000);
}
