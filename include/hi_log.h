#pragma once

#include <stdint.h>

typedef struct {
    const char *udp_ip;         // NULL = serial only
    uint16_t udp_port;
    /* Optional: called for every error line ("E (...)"), e.g. hi_notify_error. Must not block or log. */
    void (*on_error_line)(const char *line);
} hi_log_config_t;

/* Routes esp_log output to serial and UDP; the uptime "(12345)" becomes "(HH:MM:SS)" once the clock is set.
 * UDP sends are skipped from the lwIP thread (deadlock guard). Safe before WiFi is up (sends fail silently). */
void hi_log_init(const hi_log_config_t *config);
