#pragma once

#include <stdbool.h>
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
