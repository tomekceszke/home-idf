#pragma once

/*
 * Roaming decision of hi_wifi: is another AP of our SSID worth a reconnect? Pure C (no ESP-IDF), host-tested in
 * test/test_wifi_roam.c.
 */

#include <stdint.h>

typedef struct {
    uint8_t bssid[6];
    int rssi;
} hi_wifi_bss_t;

/* Index in aps of the AP to move to, or -1 to stay. aps holds one scan of our SSID. The current AP's signal is
 * taken from the same scan when it is there (like for like), otherwise link_rssi. The strongest other AP wins
 * when it beats the current one by at least min_gain_db. */
int hi_wifi_roam_pick(const hi_wifi_bss_t *aps, int n, const uint8_t current[6], int link_rssi, int min_gain_db);
