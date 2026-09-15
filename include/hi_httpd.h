#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "hi_auth.h"

typedef enum {
    HI_GUARD_PUBLIC,        // Host allowlist only
    HI_GUARD_SESSION,       // + valid session cookie
    HI_GUARD_MUTATION,      // + Origin == Host + Content-Type: application/json + X-CSRF-Token
} hi_guard_t;

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
} hi_blob_t;

typedef struct {
    hi_blob_t login_html_gz;        // served at "/" without a session (should say nothing about the device)
    hi_blob_t app_html_gz;          // served at "/" with a session
    hi_blob_t icon_png;             // "/apple-touch-icon.png", optional
    const char *manifest_json;      // "/manifest.webmanifest", optional (NUL-terminated)
} hi_httpd_ui_t;

typedef struct {
    uint16_t port;                  // 0 = 80
    uint8_t max_open_sockets;       // 0 = 7; keep <= CONFIG_LWIP_MAX_SOCKETS - 3
    uint16_t max_uri_handlers;      // 0 = 24 (includes the common routes)
    uint32_t stack_size;            // 0 = 8192 (PBKDF2 + cJSON)
    size_t max_body_len;            // 0 = 512
    const char *const *allowed_hosts;   // Host names accepted besides the current IP (DNS rebinding guard)
    size_t allowed_hosts_count;
    const hi_httpd_ui_t *ui;        // NULL = no UI routes
} hi_httpd_config_t;

/*
 * Starts the server and registers the common routes:
 *   GET  /                      login or app page (gzip, CSP, no framing)
 *   GET  /apple-touch-icon.png, /manifest.webmanifest   (if provided)
 *   GET  /api/session           {authenticated, csrf?, version}
 *   POST /api/login             {password} -> sid cookie + {csrf}; 401/429 with retry_after_ms
 *   POST /api/logout            mutation
 *   POST /api/reboot, /api/ota  mutation
 *   POST /admin/su, /admin/reboot   Authorization header
 *   GET  /admin/hw-status       {up_since, free_mem_kb, version, reset_reason}
 * Applications add their own handlers with hi_httpd_register().
 */
esp_err_t hi_httpd_start(const hi_httpd_config_t *config);

esp_err_t hi_httpd_register(const httpd_uri_t *route);

/* Returns true when the request may proceed; otherwise the error response has been sent into *result.
 * session (optional) receives the validated session for SESSION/MUTATION. */
bool hi_httpd_guard(httpd_req_t *req, hi_guard_t level, hi_auth_session_t *session, esp_err_t *result);

/* Reads and parses a JSON object body (<= max_body_len). NULL: 400/413 already sent into *result. */
cJSON *hi_httpd_read_json(httpd_req_t *req, esp_err_t *result);

/* Sends and frees json. status e.g. "200 OK". */
esp_err_t hi_httpd_send_json(httpd_req_t *req, const char *status, cJSON *json);
esp_err_t hi_httpd_send_error(httpd_req_t *req, const char *status, const char *error);

void hi_httpd_client_ip(httpd_req_t *req, char *out, size_t out_len);

/* Adds {version, idf, partition, pending_verify, reset_reason, uptime_s, up_since, time_synced, ota_running,
 * heap_kb{free,min}, wifi{connected, rssi, channel, bssid}} to obj. */
void hi_httpd_add_system_status(cJSON *obj);
