#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *timezone;       // POSIX TZ, NULL = "CET-1CEST,M3.5.0,M10.5.0/3"
    const char *servers[3];     // NULL entries skipped; all NULL = pool.ntp.org
} hi_ntp_config_t;

/* Starts SNTP in the background (non-blocking) and sets the time zone. */
void hi_ntp_start(const hi_ntp_config_t *config);

/* Wall clock is plausible (after 2020). Use esp_timer_get_time() for durations, never time(). */
bool hi_ntp_synced(void);

/* Blocks up to timeout_ms waiting for the first sync. */
bool hi_ntp_wait_synced(uint32_t timeout_ms);

/* Local date/time of boot ("%c"), or "" before the first sync. */
const char *hi_ntp_boot_time(void);
