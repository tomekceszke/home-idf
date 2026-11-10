#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    const char *ssid;
    const char *password;
    const char *hostname;       // DHCP hostname, NULL = IDF default
    bool power_save;            // false: WIFI_PS_NONE (mains powered, low latency)
    int8_t max_tx_power_qdbm;   // TX power cap in 0.25 dBm (8..84), applied on every driver start; 0 = default
} hi_wifi_config_t;

typedef struct {
    uint32_t disconnects;       // connections lost (not counting roams)
    uint32_t roams;             // reconnects to a stronger AP
    uint32_t resets;            // driver restarts after a long outage
    uint8_t last_reason;        // reason of the last disconnect or failed attempt (wifi_err_reason_t)
    uint32_t offline_s;         // length of the current outage, 0 while connected
    int8_t tx_max_qdbm;         // TX power limit in force (0.25 dBm), from the driver
} hi_wifi_stats_t;

/* Starts the station and returns immediately. Joins the strongest AP of the SSID (all-channel scan,
 * 802.11k/v) and reconnects forever with exponential backoff (CONFIG_HOME_IDF_WIFI_RETRY_MAX_S),
 * driven by an esp_timer so the system event loop is never blocked. Requires hi_nvs_init().
 *
 * A supervisor timer keeps it there: while the signal is below CONFIG_HOME_IDF_WIFI_ROAM_RSSI it scans every
 * CONFIG_HOME_IDF_WIFI_ROAM_CHECK_S and reconnects when another AP is CONFIG_HOME_IDF_WIFI_ROAM_MIN_GAIN_DB
 * stronger (stations stay on the AP they fell back to when theirs comes back); it restarts the WiFi driver,
 * not the device, after CONFIG_HOME_IDF_WIFI_RESET_AFTER_S offline; it restarts a retry chain that stopped. */
esp_err_t hi_wifi_start(const hi_wifi_config_t *config);

/* Blocks the caller up to timeout_ms; true when connected. */
bool hi_wifi_wait_connected(uint32_t timeout_ms);

bool hi_wifi_is_connected(void);

/* RSSI of the current AP, 0 when not connected. */
int hi_wifi_rssi(void);

void hi_wifi_get_stats(hi_wifi_stats_t *stats);
