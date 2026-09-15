#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"

#include "hi_ntp.h"

static const char *TAG = "HI_NTP";

#define EPOCH_2020 1577836800L

static char s_boot_time[64];

static void sync_cb(struct timeval *tv)
{
    if (s_boot_time[0] != '\0') return;
    time_t boot = tv->tv_sec - (time_t) (esp_timer_get_time() / 1000000);
    struct tm t;
    localtime_r(&boot, &t);
    strftime(s_boot_time, sizeof(s_boot_time), "%c", &t);
    ESP_LOGI(TAG, "Time synced, booted at %s", s_boot_time);
}

void hi_ntp_start(const hi_ntp_config_t *config)
{
    setenv("TZ", config->timezone ? config->timezone : "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    int n = 0;
    for (int i = 0; i < 3; i++) {
        if (config->servers[i] != NULL) esp_sntp_setservername(n++, config->servers[i]);
    }
    if (n == 0) esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sync_cb);
    esp_sntp_init();
}

bool hi_ntp_synced(void)
{
    return time(NULL) >= EPOCH_2020;
}

bool hi_ntp_wait_synced(uint32_t timeout_ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t) timeout_ms * 1000;
    while (!hi_ntp_synced() && esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return hi_ntp_synced();
}

const char *hi_ntp_boot_time(void)
{
    return s_boot_time;
}
