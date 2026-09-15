#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "hi_auth.h"
#include "hi_notify.h"
#include "hi_ntp.h"

static const char *TAG = "HI_AUTH";
static const char *NVS_NAMESPACE = "hi_auth";
static const char *NVS_KEY_HMAC = "hmac_key";
static const char *NVS_KEY_SESSIONS = "sessions_v1";
static const char *HEADER_CSRF = "X-CSRF-Token";
static const char *HEADER_AUTHORIZATION = "Authorization";

#define MAX_SESSIONS CONFIG_HOME_IDF_AUTH_MAX_SESSIONS

typedef struct {
    uint8_t token_hash[32];
    int64_t created;        // unix time; 0 = clock was not synced at login
    uint8_t used;
} stored_session_t;

static hi_auth_config_t s_config;
static SemaphoreHandle_t s_lock;
static stored_session_t s_sessions[MAX_SESSIONS];
static uint8_t s_hmac_key[32];
static uint8_t s_salt[32];
static size_t s_salt_len;
static uint8_t s_password_hash[32];
static bool s_login_enabled;
static uint32_t s_failures;
static int64_t s_locked_until_us;

/* ---------- helpers ---------- */

static bool ct_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decodes exactly out_len bytes; false unless hex is exactly 2*out_len hex chars. */
static bool hex_decode(const char *hex, uint8_t *out, size_t out_len)
{
    if (hex == NULL || strlen(hex) != out_len * 2) return false;
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_val(hex[2 * i]);
        int lo = hex_val(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t) ((hi << 4) | lo);
    }
    return true;
}

static void hex_encode(const uint8_t *in, size_t len, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 0x0F];
    }
    out[2 * len] = '\0';
}

static void sha256(const uint8_t *in, size_t len, uint8_t out[32])
{
    mbedtls_sha256(in, len, out, 0);
}

static void persist_sessions_locked(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, NVS_KEY_SESSIONS, s_sessions, sizeof(s_sessions)) == ESP_OK) {
        nvs_commit(h);
    } else {
        ESP_LOGW(TAG, "Persisting sessions failed");
    }
    nvs_close(h);
}

/* ---------- init ---------- */

void hi_auth_init(const hi_auth_config_t *config)
{
    s_config = *config;
    if (s_config.session_ttl_s == 0) s_config.session_ttl_s = 90 * 24 * 3600;
    if (s_config.lockout_max_s == 0) s_config.lockout_max_s = 300;
    s_lock = xSemaphoreCreateMutex();

    size_t salt_hex_len = s_config.password_salt_hex ? strlen(s_config.password_salt_hex) : 0;
    s_salt_len = salt_hex_len / 2;
    s_login_enabled = salt_hex_len >= 16 && salt_hex_len % 2 == 0 && s_salt_len <= sizeof(s_salt)
                      && hex_decode(s_config.password_salt_hex, s_salt, s_salt_len)
                      && hex_decode(s_config.password_hash_hex, s_password_hash, sizeof(s_password_hash))
                      && s_config.password_iterations > 0;
    if (!s_login_enabled) {
        ESP_LOGW(TAG, "No valid password hash configured: web login disabled");
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed, sessions will not persist");
        esp_fill_random(s_hmac_key, sizeof(s_hmac_key));
        return;
    }
    size_t len = sizeof(s_hmac_key);
    if (nvs_get_blob(h, NVS_KEY_HMAC, s_hmac_key, &len) != ESP_OK || len != sizeof(s_hmac_key)) {
        esp_fill_random(s_hmac_key, sizeof(s_hmac_key));
        nvs_set_blob(h, NVS_KEY_HMAC, s_hmac_key, sizeof(s_hmac_key));
        nvs_commit(h);
        ESP_LOGI(TAG, "Generated new CSRF key");
    }
    len = sizeof(s_sessions);
    if (nvs_get_blob(h, NVS_KEY_SESSIONS, s_sessions, &len) != ESP_OK || len != sizeof(s_sessions)) {
        memset(s_sessions, 0, sizeof(s_sessions));
    }
    nvs_close(h);

    int active = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) active += s_sessions[i].used ? 1 : 0;
    ESP_LOGI(TAG, "%d stored session(s)", active);
}

uint32_t hi_auth_session_ttl_s(void)
{
    return s_config.session_ttl_s;
}

/* ---------- login ---------- */

hi_auth_result_t hi_auth_login(const char *password, const char *client_ip,
                               char token_hex[HI_AUTH_TOKEN_HEX_LEN + 1], char csrf_hex[HI_AUTH_CSRF_HEX_LEN + 1],
                               uint32_t *retry_after_ms)
{
    const char *ip = client_ip ? client_ip : "?";
    *retry_after_ms = 0;
    if (!s_login_enabled) return HI_AUTH_DISABLED;

    int64_t now_us = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (now_us < s_locked_until_us) {
        *retry_after_ms = (uint32_t) ((s_locked_until_us - now_us) / 1000);
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "Login from %s while locked out", ip);
        return HI_AUTH_LOCKED;
    }
    xSemaphoreGive(s_lock);

    uint8_t derived[32];
    int rc = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const unsigned char *) password, strlen(password),
                                           s_salt, s_salt_len, s_config.password_iterations,
                                           sizeof(derived), derived);
    if (rc != 0) return HI_AUTH_ERROR;

    if (!ct_equal(derived, s_password_hash, sizeof(derived))) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_failures++;
        uint32_t shift = s_failures - 1 > 16 ? 16 : s_failures - 1;
        uint32_t delay_s = 1u << shift;
        if (delay_s > s_config.lockout_max_s) delay_s = s_config.lockout_max_s;
        s_locked_until_us = esp_timer_get_time() + (int64_t) delay_s * 1000000;
        *retry_after_ms = delay_s * 1000;
        uint32_t failures = s_failures;
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "Bad password from %s (%lu consecutive), locked %lu s",
                 ip, (unsigned long) failures, (unsigned long) delay_s);
        if (failures == 5) {
            char msg[64];
            snprintf(msg, sizeof(msg), "5 failed logins, last from %s", ip);
            hi_notify_error(msg);
        }
        return HI_AUTH_BAD_PASSWORD;
    }

    uint8_t token[32];
    esp_fill_random(token, sizeof(token));
    hex_encode(token, sizeof(token), token_hex);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_failures = 0;
    s_locked_until_us = 0;
    int slot = -1;
    for (int i = 0; i < MAX_SESSIONS && slot < 0; i++) {
        if (!s_sessions[i].used) slot = i;
    }
    if (slot < 0) {
        slot = 0;   // evict the oldest (unknown creation time counts as oldest)
        for (int i = 1; i < MAX_SESSIONS; i++) {
            if (s_sessions[i].created < s_sessions[slot].created) slot = i;
        }
    }
    stored_session_t *s = &s_sessions[slot];
    sha256(token, sizeof(token), s->token_hash);
    s->created = hi_ntp_synced() ? (int64_t) time(NULL) : 0;
    s->used = 1;
    hi_auth_session_t session;
    memcpy(session.token_hash, s->token_hash, sizeof(session.token_hash));
    persist_sessions_locked();
    xSemaphoreGive(s_lock);

    hi_auth_csrf_token(&session, csrf_hex);
    ESP_LOGW(TAG, "(not error) Login from %s", ip);
    return HI_AUTH_OK;
}

/* ---------- sessions ---------- */

bool hi_auth_session_from_request(httpd_req_t *req, hi_auth_session_t *out)
{
    char cookie[HI_AUTH_TOKEN_HEX_LEN + 2];
    size_t len = sizeof(cookie);
    if (httpd_req_get_cookie_val(req, "sid", cookie, &len) != ESP_OK) return false;
    uint8_t token[32];
    if (!hex_decode(cookie, token, sizeof(token))) return false;
    uint8_t hash[32];
    sha256(token, sizeof(token), hash);

    bool valid = false;
    bool dirty = false;
    int64_t now = hi_ntp_synced() ? (int64_t) time(NULL) : 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        stored_session_t *s = &s_sessions[i];
        if (!s->used || !ct_equal(s->token_hash, hash, sizeof(hash))) continue;
        if (now != 0 && s->created == 0) {
            s->created = now;       // first request after the clock got synced
            dirty = true;
        }
        if (now != 0 && now - s->created > (int64_t) s_config.session_ttl_s) {
            memset(s, 0, sizeof(*s));
            dirty = true;
            ESP_LOGI(TAG, "Session expired");
        } else {
            valid = true;
        }
        break;
    }
    if (dirty) persist_sessions_locked();
    xSemaphoreGive(s_lock);

    if (valid) memcpy(out->token_hash, hash, sizeof(hash));
    return valid;
}

void hi_auth_csrf_token(const hi_auth_session_t *session, char csrf_hex[HI_AUTH_CSRF_HEX_LEN + 1])
{
    uint8_t mac[32];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), s_hmac_key, sizeof(s_hmac_key),
                    session->token_hash, sizeof(session->token_hash), mac);
    hex_encode(mac, sizeof(mac), csrf_hex);
}

bool hi_auth_csrf_valid(httpd_req_t *req, const hi_auth_session_t *session)
{
    char header[HI_AUTH_CSRF_HEX_LEN + 2];
    if (httpd_req_get_hdr_value_str(req, HEADER_CSRF, header, sizeof(header)) != ESP_OK
        || strlen(header) != HI_AUTH_CSRF_HEX_LEN) {
        return false;
    }
    char expected[HI_AUTH_CSRF_HEX_LEN + 1];
    hi_auth_csrf_token(session, expected);
    return ct_equal((const uint8_t *) header, (const uint8_t *) expected, HI_AUTH_CSRF_HEX_LEN);
}

void hi_auth_logout(const hi_auth_session_t *session)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].used && ct_equal(s_sessions[i].token_hash, session->token_hash, 32)) {
            memset(&s_sessions[i], 0, sizeof(s_sessions[i]));
        }
    }
    persist_sessions_locked();
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "Logged out");
}

bool hi_auth_admin_header_valid(httpd_req_t *req)
{
    size_t expected_len = s_config.admin_header_value ? strlen(s_config.admin_header_value) : 0;
    size_t len = httpd_req_get_hdr_value_len(req, HEADER_AUTHORIZATION);
    if (expected_len == 0 || len != expected_len) {
        if (len != 0) ESP_LOGW(TAG, "Admin authorization invalid");
        return false;
    }
    char *value = malloc(len + 1);
    if (value == NULL) return false;
    bool ok = httpd_req_get_hdr_value_str(req, HEADER_AUTHORIZATION, value, len + 1) == ESP_OK
              && ct_equal((const uint8_t *) value, (const uint8_t *) s_config.admin_header_value, expected_len);
    free(value);
    if (!ok) ESP_LOGW(TAG, "Admin authorization invalid");
    return ok;
}
