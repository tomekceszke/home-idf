#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    const char *ssid;
    const char *password;
    const char *hostname;       // DHCP hostname, NULL = IDF default
    bool power_save;            // false: WIFI_PS_NONE (mains powered, low latency)
} hi_wifi_config_t;

/* Starts the station and returns immediately. Joins the strongest AP of the SSID (all-channel scan,
 * 802.11k/v) and reconnects forever with exponential backoff (CONFIG_HOME_IDF_WIFI_RETRY_MAX_S),
 * driven by an esp_timer so the system event loop is never blocked. Requires hi_nvs_init(). */
esp_err_t hi_wifi_start(const hi_wifi_config_t *config);

/* Blocks the caller up to timeout_ms; true when connected. */
bool hi_wifi_wait_connected(uint32_t timeout_ms);

bool hi_wifi_is_connected(void);

/* RSSI of the current AP, 0 when not connected. */
int hi_wifi_rssi(void);
