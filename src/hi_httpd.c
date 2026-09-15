#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"

#include "hi_health.h"
#include "hi_httpd.h"
#include "hi_ntp.h"
#include "hi_ota.h"
#include "hi_system.h"
#include "hi_wifi.h"

static const char *TAG = "HI_HTTPD";

static httpd_handle_t s_server;
static hi_httpd_config_t s_config;
static hi_httpd_ui_t s_ui;

/* ---------- helpers ---------- */

void hi_httpd_client_ip(httpd_req_t *req, char *out, size_t out_len)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    snprintf(out, out_len, "?");
    if (getpeername(httpd_req_to_sockfd(req), (struct sockaddr *) &addr, &len) == 0) {
        inet_ntop(AF_INET, &addr.sin_addr, out, out_len);
    }
}

static void set_common_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
}

esp_err_t hi_httpd_send_json(httpd_req_t *req, const char *status, cJSON *json)
{
    char *body = json ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    set_common_headers(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = body ? httpd_resp_sendstr(req, body) : httpd_resp_send_500(req);
    free(body);
    return err;
}

esp_err_t hi_httpd_send_error(httpd_req_t *req, const char *status, const char *error)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "error", error);
    return hi_httpd_send_json(req, status, json);
}

/* Host header must be our IP or an allowlisted name: blocks DNS-rebinding pages. */
static bool host_allowed(httpd_req_t *req)
{
    char host[64];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) return false;
    char *colon = strchr(host, ':');
    if (colon != NULL) {
        char port[8];
        snprintf(port, sizeof(port), ":%u", (unsigned) s_config.port);
        if (strcmp(colon, port) != 0) return false;
        *colon = '\0';
    }
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        char ip[16];
        esp_ip4addr_ntoa(&ip_info.ip, ip, sizeof(ip));
        if (strcmp(host, ip) == 0) return true;
    }
    for (size_t i = 0; i < s_config.allowed_hosts_count; i++) {
        if (strcasecmp(host, s_config.allowed_hosts[i]) == 0) return true;
    }
    return false;
}

/* If a browser sends Origin, it must be this very host. */
static bool origin_allowed(httpd_req_t *req)
{
    char origin[80];
    esp_err_t err = httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin));
    if (err == ESP_ERR_NOT_FOUND) return true;      // non-browser client or same-origin GET
    if (err != ESP_OK) return false;                // e.g. truncated: never treat an oversized Origin as absent
    char host[64];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) return false;
    char expected[80];
    snprintf(expected, sizeof(expected), "http://%s", host);
    return strcmp(origin, expected) == 0;
}

bool hi_httpd_guard(httpd_req_t *req, hi_guard_t level, hi_auth_session_t *session, esp_err_t *result)
{
    *result = ESP_OK;
    if (!host_allowed(req)) {
        *result = hi_httpd_send_error(req, "403 Forbidden", "host_not_allowed");
        return false;
    }
    if (level == HI_GUARD_PUBLIC) return true;

    hi_auth_session_t local;
    hi_auth_session_t *s = session ? session : &local;
    if (!hi_auth_session_from_request(req, s)) {
        *result = hi_httpd_send_error(req, "401 Unauthorized", "login_required");
        return false;
    }
    if (level == HI_GUARD_SESSION) return true;

    char ctype[40];
    if (!origin_allowed(req)
        || httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype)) != ESP_OK
        || strncmp(ctype, "application/json", 16) != 0
        || !hi_auth_csrf_valid(req, s)) {
        char ip[16];
        hi_httpd_client_ip(req, ip, sizeof(ip));
        ESP_LOGW(TAG, "CSRF/origin check failed for %s from %s", req->uri, ip);
        *result = hi_httpd_send_error(req, "403 Forbidden", "csrf");
        return false;
    }
    return true;
}

cJSON *hi_httpd_read_json(httpd_req_t *req, esp_err_t *result)
{
    *result = ESP_OK;
    if (req->content_len > s_config.max_body_len) {
        *result = hi_httpd_send_error(req, "413 Payload Too Large", "body_too_large");
        return NULL;
    }
    char *buf = malloc(req->content_len + 1);
    if (buf == NULL) {
        *result = hi_httpd_send_error(req, "503 Service Unavailable", "out_of_memory");
        return NULL;
    }
    size_t received = 0;
    int timeouts = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, buf + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
        if (n <= 0) {
            free(buf);
            *result = ESP_FAIL;
            return NULL;
        }
        received += (size_t) n;
    }
    buf[received] = '\0';
    cJSON *json = received ? cJSON_Parse(buf) : cJSON_CreateObject();
    free(buf);
    if (json == NULL || !cJSON_IsObject(json)) {
        cJSON_Delete(json);
        *result = hi_httpd_send_error(req, "400 Bad Request", "invalid_json");
        return NULL;
    }
    return json;
}

void hi_httpd_add_system_status(cJSON *root)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    cJSON_AddStringToObject(root, "version", app->version);
    cJSON_AddStringToObject(root, "idf", app->idf_ver);
    cJSON_AddStringToObject(root, "partition", running ? running->label : "?");
    cJSON_AddBoolToObject(root, "pending_verify", hi_health_pending_verify());
    cJSON_AddStringToObject(root, "reset_reason", hi_reset_reason());
    cJSON_AddNumberToObject(root, "uptime_s", (double) (esp_timer_get_time() / 1000000));
    cJSON_AddStringToObject(root, "up_since", hi_ntp_boot_time());
    cJSON_AddBoolToObject(root, "time_synced", hi_ntp_synced());
    cJSON_AddBoolToObject(root, "ota_running", hi_ota_is_running());

    cJSON *heap = cJSON_AddObjectToObject(root, "heap_kb");
    cJSON_AddNumberToObject(heap, "free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    cJSON_AddNumberToObject(heap, "min", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024);

    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddBoolToObject(wifi, "connected", hi_wifi_is_connected());
    wifi_ap_record_t ap;
    if (hi_wifi_is_connected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        char bssid[18];
        snprintf(bssid, sizeof(bssid), MACSTR, MAC2STR(ap.bssid));
        cJSON_AddNumberToObject(wifi, "rssi", ap.rssi);
        cJSON_AddNumberToObject(wifi, "channel", ap.primary);
        cJSON_AddStringToObject(wifi, "bssid", bssid);
    } else {
        cJSON_AddNumberToObject(wifi, "rssi", 0);
    }
}

/* ---------- UI ---------- */

static esp_err_t index_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_PUBLIC, NULL, &result)) return result;

    // Without a session the client gets only the login page: nothing about the device in the markup
    hi_auth_session_t session;
    bool authenticated = hi_auth_session_from_request(req, &session);
    const hi_blob_t *page = authenticated ? &s_ui.app_html_gz : &s_ui.login_html_gz;

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Vary", "Cookie");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'self'; img-src 'self' blob: data:; style-src 'unsafe-inline'; "
                       "script-src 'unsafe-inline'; connect-src 'self'; manifest-src 'self' data:; "
                       "frame-ancestors 'none'; form-action 'self'; base-uri 'none'");
    return httpd_resp_send(req, (const char *) page->start, page->end - page->start);
}

static esp_err_t icon_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_PUBLIC, NULL, &result)) return result;
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=604800");
    return httpd_resp_send(req, (const char *) s_ui.icon_png.start, s_ui.icon_png.end - s_ui.icon_png.start);
}

static esp_err_t manifest_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_PUBLIC, NULL, &result)) return result;
    httpd_resp_set_type(req, "application/manifest+json");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    return httpd_resp_sendstr(req, s_ui.manifest_json);
}

/* ---------- session ---------- */

static esp_err_t session_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_PUBLIC, NULL, &result)) return result;
    hi_auth_session_t session;
    cJSON *json = cJSON_CreateObject();
    bool authenticated = hi_auth_session_from_request(req, &session);
    cJSON_AddBoolToObject(json, "authenticated", authenticated);
    if (authenticated) {
        char csrf[HI_AUTH_CSRF_HEX_LEN + 1];
        hi_auth_csrf_token(&session, csrf);
        cJSON_AddStringToObject(json, "csrf", csrf);
    }
    cJSON_AddStringToObject(json, "version", esp_app_get_description()->version);
    return hi_httpd_send_json(req, "200 OK", json);
}

static esp_err_t login_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_PUBLIC, NULL, &result)) return result;
    if (!origin_allowed(req)) return hi_httpd_send_error(req, "403 Forbidden", "origin");

    cJSON *body = hi_httpd_read_json(req, &result);
    if (body == NULL) return result;
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(body, "password");
    if (!cJSON_IsString(password) || strlen(password->valuestring) > 128) {
        cJSON_Delete(body);
        return hi_httpd_send_error(req, "400 Bad Request", "password_required");
    }

    char ip[16];
    hi_httpd_client_ip(req, ip, sizeof(ip));
    char token[HI_AUTH_TOKEN_HEX_LEN + 1];
    char csrf[HI_AUTH_CSRF_HEX_LEN + 1];
    uint32_t retry_after_ms = 0;
    hi_auth_result_t r = hi_auth_login(password->valuestring, ip, token, csrf, &retry_after_ms);
    cJSON_Delete(body);

    cJSON *json = cJSON_CreateObject();
    switch (r) {
        case HI_AUTH_OK: {
            char cookie[160];
            snprintf(cookie, sizeof(cookie), "sid=%s; Path=/; Max-Age=%lu; HttpOnly; SameSite=Strict",
                     token, (unsigned long) hi_auth_session_ttl_s());
            httpd_resp_set_hdr(req, "Set-Cookie", cookie);  // pointer kept by httpd: send within this scope
            cJSON_AddStringToObject(json, "csrf", csrf);
            return hi_httpd_send_json(req, "200 OK", json);
        }
        case HI_AUTH_BAD_PASSWORD:
        case HI_AUTH_LOCKED:
            cJSON_AddStringToObject(json, "error", r == HI_AUTH_LOCKED ? "locked" : "bad_password");
            cJSON_AddNumberToObject(json, "retry_after_ms", retry_after_ms);
            return hi_httpd_send_json(req, r == HI_AUTH_LOCKED ? "429 Too Many Requests" : "401 Unauthorized", json);
        case HI_AUTH_DISABLED:
            cJSON_AddStringToObject(json, "error", "login_disabled");
            return hi_httpd_send_json(req, "503 Service Unavailable", json);
        default:
            cJSON_AddStringToObject(json, "error", "internal");
            return hi_httpd_send_json(req, "500 Internal Server Error", json);
    }
}

static esp_err_t logout_handler(httpd_req_t *req)
{
    esp_err_t result;
    hi_auth_session_t session;
    if (!hi_httpd_guard(req, HI_GUARD_MUTATION, &session, &result)) return result;
    hi_auth_logout(&session);
    httpd_resp_set_hdr(req, "Set-Cookie", "sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    return hi_httpd_send_json(req, "200 OK", cJSON_CreateObject());
}

static esp_err_t reboot_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_MUTATION, NULL, &result)) return result;
    ESP_LOGW(TAG, "(not error) Reboot requested from UI");
    hi_restart_soon(500);
    return hi_httpd_send_json(req, "202 Accepted", cJSON_CreateObject());
}

static esp_err_t ota_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_MUTATION, NULL, &result)) return result;
    bool started = hi_ota_start_background();
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "started", started);
    return hi_httpd_send_json(req, started ? "202 Accepted" : "409 Conflict", json);
}

/* ---------- admin (scripts) ---------- */

static esp_err_t admin_denied(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_status(req, "401 Unauthorized");
    return httpd_resp_send(req, "", 0);
}

static esp_err_t admin_su_handler(httpd_req_t *req)
{
    if (!hi_auth_admin_header_valid(req)) return admin_denied(req);
    bool started = hi_ota_start_background();
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_sendstr(req, started ? "Upgrade in progress..." : "Upgrade already running");
}

static esp_err_t admin_reboot_handler(httpd_req_t *req)
{
    if (!hi_auth_admin_header_valid(req)) return admin_denied(req);
    ESP_LOGW(TAG, "(not error) Reboot requested by admin");
    hi_restart_soon(500);
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_sendstr(req, "Reboot in progress...");
}

static esp_err_t admin_hw_status_handler(httpd_req_t *req)
{
    char data[256];
    snprintf(data, sizeof(data),
             "{\"up_since\":\"%s\",\"free_mem_kb\":\"%u\",\"version\":\"%s\",\"reset_reason\":\"%s\"}",
             hi_ntp_boot_time(), (unsigned) (esp_get_free_heap_size() / 1024),
             esp_app_get_description()->version, hi_reset_reason());
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, data);
}

/* ---------- server ---------- */

esp_err_t hi_httpd_register(const httpd_uri_t *route)
{
    esp_err_t err = httpd_register_uri_handler(s_server, route);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Registering %s failed: %s", route->uri, esp_err_to_name(err));
    }
    return err;
}

esp_err_t hi_httpd_start(const hi_httpd_config_t *config)
{
    s_config = *config;
    if (s_config.port == 0) s_config.port = 80;
    if (s_config.max_body_len == 0) s_config.max_body_len = 512;
    if (config->ui != NULL) s_ui = *config->ui;

    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.server_port = s_config.port;
    http.max_open_sockets = s_config.max_open_sockets ? s_config.max_open_sockets : 7;
    http.max_uri_handlers = s_config.max_uri_handlers ? s_config.max_uri_handlers : 24;
    http.stack_size = s_config.stack_size ? s_config.stack_size : 8192;     // PBKDF2 + cJSON
    http.lru_purge_enable = true;
    http.recv_wait_timeout = 5;
    http.send_wait_timeout = 5;

    esp_err_t err = httpd_start(&s_server, &http);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t common[] = {
        {.uri = "/api/session", .method = HTTP_GET, .handler = session_handler},
        {.uri = "/api/login", .method = HTTP_POST, .handler = login_handler},
        {.uri = "/api/logout", .method = HTTP_POST, .handler = logout_handler},
        {.uri = "/api/reboot", .method = HTTP_POST, .handler = reboot_handler},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = ota_handler},
        {.uri = "/admin/su", .method = HTTP_POST, .handler = admin_su_handler},
        {.uri = "/admin/reboot", .method = HTTP_POST, .handler = admin_reboot_handler},
        {.uri = "/admin/hw-status", .method = HTTP_GET, .handler = admin_hw_status_handler},
    };
    for (size_t i = 0; i < sizeof(common) / sizeof(common[0]); i++) {
        hi_httpd_register(&common[i]);
    }
    if (s_ui.login_html_gz.start != NULL && s_ui.app_html_gz.start != NULL) {
        hi_httpd_register(&(httpd_uri_t) {.uri = "/", .method = HTTP_GET, .handler = index_handler});
    }
    if (s_ui.icon_png.start != NULL) {
        hi_httpd_register(&(httpd_uri_t) {.uri = "/apple-touch-icon.png", .method = HTTP_GET, .handler = icon_handler});
    }
    if (s_ui.manifest_json != NULL) {
        hi_httpd_register(&(httpd_uri_t) {.uri = "/manifest.webmanifest", .method = HTTP_GET,
                                          .handler = manifest_handler});
    }
    ESP_LOGI(TAG, "HTTP server started on port %u", (unsigned) s_config.port);
    return ESP_OK;
}
