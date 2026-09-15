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

static const char *TAG = "HI_WIFI";

#define CONNECTED_BIT BIT0

static EventGroupHandle_t s_events;
static esp_timer_handle_t s_retry_timer;
static int s_retry_delay_s = 1;
static volatile bool s_connected = false;

static void retry_cb(void *arg)
{
    ESP_LOGI(TAG, "Connecting...");
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
    }
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *) data;
        s_connected = false;
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        s_retry_delay_s = s_retry_delay_s * 2 < CONFIG_HOME_IDF_WIFI_RETRY_MAX_S
                          ? s_retry_delay_s * 2 : CONFIG_HOME_IDF_WIFI_RETRY_MAX_S;
        ESP_LOGI(TAG, "Disconnected (reason %d), connecting in %d s", event->reason, s_retry_delay_s);
        // Never sleep here: this runs in the system event loop task shared by all event handlers
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, (uint64_t) s_retry_delay_s * 1000000);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) data;
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGI(TAG, "Got IP " IPSTR " from AP " MACSTR ", channel %d, rssi %d",
                     IP2STR(&event->ip_info.ip), MAC2STR(ap.bssid), ap.primary, ap.rssi);
        } else {
            ESP_LOGI(TAG, "Got IP " IPSTR, IP2STR(&event->ip_info.ip));
        }
        s_retry_delay_s = 1;
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
