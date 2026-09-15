/* Minimal home-idf device: WiFi, UDP log, NTP, OTA, ntfy, health verification and a login-protected page. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials-example.h"   // CI build
#endif

#include "hi_auth.h"
#include "hi_health.h"
#include "hi_httpd.h"
#include "hi_log.h"
#include "hi_notify.h"
#include "hi_ntp.h"
#include "hi_ota.h"
#include "hi_system.h"
#include "hi_wifi.h"
#if CONFIG_HOME_IDF_GCP
#include "hi_gcp.h"
#endif

static const char *TAG = "MAIN";

extern const uint8_t login_html_gz_start[] asm("_binary_login_html_gz_start");
extern const uint8_t login_html_gz_end[] asm("_binary_login_html_gz_end");
extern const uint8_t app_html_gz_start[] asm("_binary_app_html_gz_start");
extern const uint8_t app_html_gz_end[] asm("_binary_app_html_gz_end");

static const char OTA_CERT_PEM[] = "-----BEGIN CERTIFICATE-----\n(your OTA server certificate)\n-----END CERTIFICATE-----\n";

static bool healthy(void)
{
    return hi_wifi_is_connected();
}

static esp_err_t hello_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_SESSION, NULL, &result)) return result;
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "hello", "world");
    hi_httpd_add_system_status(json);
    return hi_httpd_send_json(req, "200 OK", json);
}

void app_main(void)
{
    hi_health_early_boot(3);
    ESP_LOGI(TAG, "Reset reason: %s", hi_reset_reason());

    hi_log_init(&(hi_log_config_t) {.udp_ip = "192.168.1.10", .udp_port = 1337, .on_error_line = hi_notify_error});
    hi_wifi_start(&(hi_wifi_config_t) {.ssid = WIFI_SSID, .password = WIFI_PASS, .hostname = "home-idf-demo"});
    hi_notify_init(&(hi_notify_config_t) {
        .topic = NTFY_TOPIC, .error_topic = NTFY_ERROR_TOPIC, .error_title = "Demo error", .error_cooldown_s = 3600,
    });
    hi_ntp_start(&(hi_ntp_config_t) {.servers = {"pool.ntp.org"}});
    hi_ota_init(&(hi_ota_config_t) {
        .url = "https://192.168.1.10:8070/home-idf-minimal.bin", .cert_pem = OTA_CERT_PEM, .delete_after = true,
    });

    hi_wifi_wait_connected(30000);
    hi_auth_init(&(hi_auth_config_t) {
        .password_iterations = AUTH_PASSWORD_ITERATIONS,
        .password_salt_hex = AUTH_PASSWORD_SALT_HEX,
        .password_hash_hex = AUTH_PASSWORD_HASH_HEX,
        .admin_header_value = HEADER_AUTHORIZATION_VALUE,
    });

    static const char *const hosts[] = {"home-idf-demo", "home-idf-demo.lan"};
    static const hi_httpd_ui_t ui = {
        .login_html_gz = {login_html_gz_start, login_html_gz_end},
        .app_html_gz = {app_html_gz_start, app_html_gz_end},
    };
    hi_httpd_start(&(hi_httpd_config_t) {.allowed_hosts = hosts, .allowed_hosts_count = 2, .ui = &ui});
    hi_httpd_register(&(httpd_uri_t) {.uri = "/api/hello", .method = HTTP_GET, .handler = hello_handler});

    hi_health_start(&(hi_health_config_t) {.is_healthy = healthy});
    hi_ota_start_background();

#if CONFIG_HOME_IDF_GCP
    hi_gcp_init(&(hi_gcp_config_t) {
        .service_account_email = "device@project.iam.gserviceaccount.com",
        .private_key_pem = "-----BEGIN PRIVATE KEY-----\n(key)\n-----END PRIVATE KEY-----\n",
    });
#endif
    hi_notify_event("Demo", "Device started");
}
