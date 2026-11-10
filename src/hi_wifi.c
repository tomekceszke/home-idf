#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#include "hi_wifi.h"
#include "hi_wifi_roam.h"

static const char *TAG = "HI_WIFI";

#define CONNECTED_BIT     BIT0
#define SUPERVISE_S       30        // period of the supervisor (roam check, stalled retries, driver restart)
#define ROAM_SCAN_MAX_APS 8         // APs of our SSID kept from one scan (the strongest first)
#define US                1000000LL
#define FULL_TX_QDBM      80        // 20 dBm, the driver's maximum

static EventGroupHandle_t s_events;
static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_supervise_timer;
static int s_retry_delay_s = 1;
static volatile bool s_connected = false;
static char s_ssid[33];
static int8_t s_max_tx_power_qdbm;

// Supervisor state: written by the event loop and the esp_timer task, read anywhere. A torn read costs at most
// one skipped roam check or one early retry, so plain volatile is enough.
static volatile bool s_roam_scan;           // our scan is running
static volatile bool s_roaming;             // we disconnected on purpose to join a stronger AP
static volatile bool s_restarting;          // driver restart: its disconnect event must not arm a retry
static volatile int64_t s_down_since_us;    // start of the current outage, 0 while connected
static volatile int64_t s_last_attempt_us;
static int64_t s_last_reset_us;
static int64_t s_last_roam_check_us;
static volatile uint32_t s_disconnects, s_roams, s_resets;
static volatile uint8_t s_last_reason;

/* After esp_wifi_start() has returned ("after WiFi start" in the IDF docs), at boot and after a driver restart,
 * the same point where applications used to set it themselves. */
static void apply_tx_power(void)
{
    if (s_max_tx_power_qdbm <= 0) return;
    esp_err_t err = esp_wifi_set_max_tx_power(s_max_tx_power_qdbm);
    if (err != ESP_OK) ESP_LOGW(TAG, "TX power cap %d qdBm: %s", s_max_tx_power_qdbm, esp_err_to_name(err));
}

static void connect_now(void)
{
    s_last_attempt_us = esp_timer_get_time();
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && !s_connected) {
        // No disconnect event follows a refused call, so re-arm here: the retries must never stop
        ESP_LOGW(TAG, "esp_wifi_connect: %s, again in %d s", esp_err_to_name(err), s_retry_delay_s);
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, (uint64_t) s_retry_delay_s * US);
    }
}

static void retry_cb(void *arg)
{
    if (s_connected) return;        // a late timer after a reconnect: connecting again would drop the link
    ESP_LOGI(TAG, "Connecting...");
    connect_now();
}

/* Scan below this signal. The AP hears a station with a TX cap weaker by the cap, so the trigger rises by it:
 * at 8.5 dBm (meters, heatronic) -64 instead of -75 (heatronic heard its AP at -67 while the AP heard it at -86). */
static int roam_rssi(void)
{
    int cap_db = s_max_tx_power_qdbm > 0 && s_max_tx_power_qdbm < FULL_TX_QDBM
                 ? (FULL_TX_QDBM - s_max_tx_power_qdbm) / 4 : 0;
    return CONFIG_HOME_IDF_WIFI_ROAM_RSSI + cap_db;
}

static void start_roam_scan(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK || ap.rssi >= roam_rssi()) return;
    wifi_scan_config_t scan = {
        .ssid = (uint8_t *) s_ssid,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    s_roam_scan = true;
    esp_err_t err = esp_wifi_scan_start(&scan, false);
    if (err != ESP_OK) {
        s_roam_scan = false;
        ESP_LOGW(TAG, "Roam scan not started: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGD(TAG, "Signal %d dBm below %d: looking for a stronger AP", ap.rssi, roam_rssi());
}

static void roam_scan_done(void)
{
    static wifi_ap_record_t records[ROAM_SCAN_MAX_APS];     // event loop only: kept off its stack
    hi_wifi_bss_t aps[ROAM_SCAN_MAX_APS];
    uint16_t n = ROAM_SCAN_MAX_APS;
    s_roam_scan = false;
    if (esp_wifi_scan_get_ap_records(&n, records) != ESP_OK) return;   // also frees the driver's list
    wifi_ap_record_t current;
    if (!s_connected || esp_wifi_sta_get_ap_info(&current) != ESP_OK) return;
    for (int i = 0; i < n; i++) {
        memcpy(aps[i].bssid, records[i].bssid, 6);
        aps[i].rssi = records[i].rssi;
    }
    int pick = hi_wifi_roam_pick(aps, n, current.bssid, current.rssi, CONFIG_HOME_IDF_WIFI_ROAM_MIN_GAIN_DB);
    if (pick < 0) {
        ESP_LOGI(TAG, "No AP stronger by %d dB than " MACSTR " (%d dBm, %d APs seen)",
                 CONFIG_HOME_IDF_WIFI_ROAM_MIN_GAIN_DB, MAC2STR(current.bssid), current.rssi, n);
        return;
    }
    ESP_LOGW(TAG, "Roaming: " MACSTR " %d dBm -> " MACSTR " %d dBm, reconnecting", MAC2STR(current.bssid),
             current.rssi, MAC2STR(aps[pick].bssid), aps[pick].rssi);
    s_roams++;
    s_roaming = true;
    // The reconnect scans every channel and joins the strongest AP, which is the one just found
    esp_wifi_disconnect();
}

static void supervise_cb(void *arg)
{
    int64_t now = esp_timer_get_time();
    if (s_connected) {
        if (CONFIG_HOME_IDF_WIFI_ROAM_CHECK_S > 0 && !s_roam_scan
            && now - s_last_roam_check_us >= CONFIG_HOME_IDF_WIFI_ROAM_CHECK_S * US) {
            s_last_roam_check_us = now;
            start_roam_scan();
        }
        return;
    }
    int64_t down_since = s_down_since_us;
    if (CONFIG_HOME_IDF_WIFI_RESET_AFTER_S > 0 && down_since != 0
        && now - down_since >= CONFIG_HOME_IDF_WIFI_RESET_AFTER_S * US
        && now - s_last_reset_us >= CONFIG_HOME_IDF_WIFI_RESET_AFTER_S * US) {
        // Long outage although the APs may be fine: restart the driver (not the device, it keeps its state)
        s_last_reset_us = now;
        s_resets++;
        ESP_LOGW(TAG, "Offline for %lld s (last reason %u): restarting the WiFi driver",
                 (long long) ((now - down_since) / US), s_last_reason);
        esp_timer_stop(s_retry_timer);
        s_roam_scan = false;
        s_restarting = true;    // cleared by STA_START, which the event loop handles after the stop's events
        esp_wifi_stop();
        esp_wifi_start();       // STA_START connects
        apply_tx_power();
        return;
    }
    // Safety net: a retry chain that broke without an event (no timer armed and no attempt for a while)
    if (!esp_timer_is_active(s_retry_timer)
        && now - s_last_attempt_us >= (CONFIG_HOME_IDF_WIFI_RETRY_MAX_S + SUPERVISE_S) * US) {
        ESP_LOGW(TAG, "No connection attempt for %lld s: connecting", (long long) ((now - s_last_attempt_us) / US));
        connect_now();
    }
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        s_restarting = false;
        connect_now();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *) data;
        bool was_connected = s_connected;
        s_connected = false;
        s_last_reason = event->reason;
        if (s_down_since_us == 0) s_down_since_us = esp_timer_get_time();
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        // Never sleep here: this runs in the system event loop task shared by all event handlers
        esp_timer_stop(s_retry_timer);
        if (s_restarting) return;
        if (s_roaming) {
            s_roaming = false;
            s_retry_delay_s = 1;
            esp_timer_start_once(s_retry_timer, 100 * 1000);
            return;
        }
        if (was_connected) s_disconnects++;
        s_retry_delay_s = s_retry_delay_s * 2 < CONFIG_HOME_IDF_WIFI_RETRY_MAX_S
                          ? s_retry_delay_s * 2 : CONFIG_HOME_IDF_WIFI_RETRY_MAX_S;
        ESP_LOGI(TAG, "Disconnected (reason %d), connecting in %d s", event->reason, s_retry_delay_s);
        esp_timer_start_once(s_retry_timer, (uint64_t) s_retry_delay_s * US);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        if (s_roam_scan) roam_scan_done();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        // Still associated but the DHCP lease ran out (renewals lost on a weak link): useless, so start over,
        // which also picks the strongest AP again
        ESP_LOGW(TAG, "Lost the IP address while associated: reconnecting");
        esp_wifi_disconnect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) data;
        int64_t down_s = s_down_since_us ? (esp_timer_get_time() - s_down_since_us) / US : 0;
        wifi_ap_record_t ap;
        int8_t tx_qdbm = 0;
        esp_wifi_get_max_tx_power(&tx_qdbm);
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGI(TAG, "Got IP " IPSTR " from AP " MACSTR ", channel %d, rssi %d, tx max %d qdBm, after %lld s offline",
                     IP2STR(&event->ip_info.ip), MAC2STR(ap.bssid), ap.primary, ap.rssi, tx_qdbm, (long long) down_s);
        } else {
            ESP_LOGI(TAG, "Got IP " IPSTR " after %lld s offline", IP2STR(&event->ip_info.ip), (long long) down_s);
        }
        s_retry_delay_s = 1;
        s_down_since_us = 0;
        s_last_roam_check_us = esp_timer_get_time();     // first roam check one period after joining
        s_connected = true;
        xEventGroupSetBits(s_events, CONNECTED_BIT);
    }
}

esp_err_t hi_wifi_start(const hi_wifi_config_t *config)
{
    if (s_events != NULL) return ESP_ERR_INVALID_STATE;
    s_events = xEventGroupCreate();
    const esp_timer_create_args_t timer_args = {.callback = retry_cb, .name = "hi_wifi_retry"};
    esp_err_t err = esp_timer_create(&timer_args, &s_retry_timer);
    if (err != ESP_OK) return err;
    const esp_timer_create_args_t supervise_args = {.callback = supervise_cb, .name = "hi_wifi_supervise"};
    err = esp_timer_create(&supervise_args, &s_supervise_timer);
    if (err != ESP_OK) return err;
    strlcpy(s_ssid, config->ssid, sizeof(s_ssid));
    s_max_tx_power_qdbm = config->max_tx_power_qdbm;
    s_down_since_us = esp_timer_get_time();

    ESP_ERROR_CHECK(esp_netif_init());
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (config->hostname != NULL) {
        esp_netif_set_hostname(netif, config->hostname);
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            // Several APs may broadcast the SSID: scan every channel and join the strongest one
            // (the default fast scan takes the first match, e.g. an AP on another floor)
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .failure_retry_cnt = 2,
            // 802.11k/v: let the network steer the station to a better AP later
            .rm_enabled = 1,
            .btm_enabled = 1,
        },
    };
    strlcpy((char *) wifi_config.sta.ssid, config->ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *) wifi_config.sta.password, config->password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    // Modem sleep adds 50-150 ms latency; controllers are mains powered
    esp_wifi_set_ps(config->power_save ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
    apply_tx_power();
    esp_timer_start_periodic(s_supervise_timer, SUPERVISE_S * US);
    ESP_LOGI(TAG, "Station started, SSID %s", config->ssid);
    return ESP_OK;
}

bool hi_wifi_wait_connected(uint32_t timeout_ms)
{
    if (s_events == NULL) return false;
    EventBits_t bits = xEventGroupWaitBits(s_events, CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return (bits & CONNECTED_BIT) != 0;
}

bool hi_wifi_is_connected(void)
{
    return s_connected;
}

int hi_wifi_rssi(void)
{
    wifi_ap_record_t ap;
    return s_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

void hi_wifi_get_stats(hi_wifi_stats_t *stats)
{
    int64_t down_since = s_down_since_us;
    stats->disconnects = s_disconnects;
    stats->roams = s_roams;
    stats->resets = s_resets;
    stats->last_reason = s_last_reason;
    stats->offline_s = down_since ? (uint32_t) ((esp_timer_get_time() - down_since) / US) : 0;
    int8_t tx = 0;
    stats->tx_max_qdbm = esp_wifi_get_max_tx_power(&tx) == ESP_OK ? tx : 0;
}
