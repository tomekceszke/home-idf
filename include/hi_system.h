#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* nvs_flash_init(); erases and re-inits when the partition is full or from a newer IDF.
 * Idempotent. *erased (optional) tells the caller that stored state is gone. */
esp_err_t hi_nvs_init(bool *erased);

/* Human-readable esp_reset_reason(). */
const char *hi_reset_reason(void);

/* True for panic, watchdog and brownout resets. */
bool hi_reset_was_unexpected(void);

/* Restart after delay_ms from an esp_timer, so an HTTP response can still go out. */
void hi_restart_soon(uint32_t delay_ms);

/* ESP-IDF version the bootloader in flash was built with ("v5.4.2"). False when it carries no description
 * (bootloaders older than ESP-IDF 5.1) or flash cannot be read; out is then "". */
bool hi_bootloader_idf(char *out, size_t out_size);
