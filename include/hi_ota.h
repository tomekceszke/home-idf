#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    const char *url;            // https://host:port/<name>.bin
    const char *cert_pem;       // trust anchor of the OTA server (NUL-terminated)
    bool delete_after;          // send DELETE for the bin after a successful update
} hi_ota_config_t;

void hi_ota_init(const hi_ota_config_t *config);

/* Downloads and installs the image; on success deletes the bin (if configured) and restarts.
 * Returns only on failure (or ESP_ERR_NOT_FOUND-like errors when no image is published). */
esp_err_t hi_ota_run(void);

/* hi_ota_run() in its own task; false when already running or not initialised. */
bool hi_ota_start_background(void);

bool hi_ota_is_running(void);
