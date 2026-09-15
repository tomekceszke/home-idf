#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_http_server.h"

#define HI_AUTH_TOKEN_HEX_LEN 64    // + NUL
#define HI_AUTH_CSRF_HEX_LEN  64    // + NUL

typedef enum {
    HI_AUTH_OK = 0,
    HI_AUTH_BAD_PASSWORD,
    HI_AUTH_LOCKED,         // retry_after_ms set
    HI_AUTH_DISABLED,       // no valid password hash configured
    HI_AUTH_ERROR,
} hi_auth_result_t;

typedef struct {
    uint8_t token_hash[32];
} hi_auth_session_t;

typedef struct {
    /* tools/hash_password.py output. Empty or invalid salt/hash disables web login. */
    uint32_t password_iterations;
    const char *password_salt_hex;
    const char *password_hash_hex;
    /* Authorization header value for admin endpoints (scripts). Empty or NULL locks them. */
    const char *admin_header_value;
    uint32_t session_ttl_s;         // 0 = 90 days
    uint32_t lockout_max_s;         // failed logins back off 1, 2, 4 ... s up to this; 0 = 300
} hi_auth_config_t;

/* Requires NVS and a started WiFi (true RNG). Sessions: only SHA-256 of the token is stored (NVS),
 * CSRF token = HMAC-SHA256(device key, session hash). */
void hi_auth_init(const hi_auth_config_t *config);

hi_auth_result_t hi_auth_login(const char *password, const char *client_ip,
                               char token_hex[HI_AUTH_TOKEN_HEX_LEN + 1], char csrf_hex[HI_AUTH_CSRF_HEX_LEN + 1],
                               uint32_t *retry_after_ms);
/* Validates the "sid" cookie. */
bool hi_auth_session_from_request(httpd_req_t *req, hi_auth_session_t *out);
/* Constant-time check of the X-CSRF-Token header against the session. */
bool hi_auth_csrf_valid(httpd_req_t *req, const hi_auth_session_t *session);
void hi_auth_csrf_token(const hi_auth_session_t *session, char csrf_hex[HI_AUTH_CSRF_HEX_LEN + 1]);
void hi_auth_logout(const hi_auth_session_t *session);
/* Authorization header equals admin_header_value (constant time). */
bool hi_auth_admin_header_valid(httpd_req_t *req);
uint32_t hi_auth_session_ttl_s(void);
