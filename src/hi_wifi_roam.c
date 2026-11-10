#include <string.h>

#include "hi_wifi_roam.h"

int hi_wifi_roam_pick(const hi_wifi_bss_t *aps, int n, const uint8_t current[6], int link_rssi, int min_gain_db)
{
    int current_rssi = link_rssi;
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (memcmp(aps[i].bssid, current, 6) == 0) {
            current_rssi = aps[i].rssi;
        } else if (best < 0 || aps[i].rssi > aps[best].rssi) {
            best = i;
        }
    }
    return best >= 0 && aps[best].rssi >= current_rssi + min_gain_db ? best : -1;
}
