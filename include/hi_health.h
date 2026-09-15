#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Verification of a new firmware image, independent of power quality.
 *
 * The bootloader's own rollback treats any reset before confirmation as a failed image, so a single brownout
 * during the first WiFi calibration would roll a good image back. hi_health_early_boot() therefore confirms the
 * image to the bootloader right away (the bootloader still catches images that die before app_main) and this
 * module verifies it instead: an unverified image must report healthy within verify_timeout_s and may not reset
 * more than max_unverified_boots times (brownout and power-on resets are not counted). Otherwise the previous
 * OTA slot is booted.
 */

typedef struct {
    bool (*is_healthy)(void);       // application predicate for a new image (e.g. core task alive + WiFi)
    void (*tick)(void);             // optional, called every second from the health task
    void (*log_stats)(void);        // optional, called every stats_period_s
    uint32_t verify_timeout_s;      // 0 = 300
    uint32_t stats_period_s;        // 0 = 300
} hi_health_config_t;

/* Call first thing in app_main (initialises NVS). max_unverified_boots: 0 = 3. May roll back and restart. */
void hi_health_early_boot(uint8_t max_unverified_boots);

/* Starts the 1 s health task; notifies about unexpected resets (hi_notify_error). */
void hi_health_start(const hi_health_config_t *config);

/* True while a freshly updated image still waits for verification. */
bool hi_health_pending_verify(void);
