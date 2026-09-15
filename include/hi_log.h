#pragma once

#include <stdint.h>

typedef struct {
    const char *udp_ip;         // NULL = serial only
    uint16_t udp_port;
    /* Optional: called for every error line ("E (...)"), e.g. hi_notify_error. Must not block or log. */
    void (*on_error_line)(const char *line);
} hi_log_config_t;

/* Routes esp_log output to serial and UDP; the uptime "(12345)" becomes "(HH:MM:SS)" once the clock is set.
 * Logging never waits for the network: lines go into a ring buffer sent by a low-priority task and are dropped
 * when it is full (also avoids the lwIP-thread deadlock). May be called before hi_wifi_start(): UDP sending
 * starts once WiFi is connected, earlier lines wait in the buffer. */
void hi_log_init(const hi_log_config_t *config);
