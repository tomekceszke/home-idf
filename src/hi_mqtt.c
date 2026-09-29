#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_client.h"

#include "hi_mqtt.h"

static const char *TAG = "HI_MQTT";

#define TOPIC_MAX_LEN 64
#define CLIENT_ID_MAX_LEN 24
#define DEFAULT_OUTBOX_LIMIT_BYTES 24576
#define DEFAULT_RECONNECT_TIMEOUT_MS 30000
#define DEFAULT_STATE_PERIOD_S 60
#define DEFAULT_STATE_STACK 4096

static esp_mqtt_client_handle_t s_client;
static hi_mqtt_state_fn_t s_state_fn;
static hi_mqtt_connection_fn_t s_on_connection;
static TaskHandle_t s_state_task;
static uint32_t s_state_period_s;
static char s_device_id[13];
static char s_client_id[CLIENT_ID_MAX_LEN];
static char s_prefix[24];
static char s_status_topic[TOPIC_MAX_LEN];
static char s_state_topic[TOPIC_MAX_LEN];
static volatile bool s_connected;
static volatile uint32_t s_dropped;

typedef struct {
    char topic[TOPIC_MAX_LEN];
    char kind[16];
    size_t max_len;
    char *buf;
    hi_mqtt_message_fn_t cb;
} subscription_t;

static subscription_t s_subs[HI_MQTT_MAX_SUBSCRIPTIONS];
static int s_sub_count;
static int s_rx_sub = -1;           // subscription receiving a fragmented message, -1 = none or dropped

const char *hi_mqtt_device_id(void)
{
    return s_device_id;
}

bool hi_mqtt_enabled(void)
{
    return s_client != NULL;
}

static int publish_topic(const char *topic, const char *payload, int qos, bool retain)
{
    if (s_client == NULL) return -1;
    const int msg_id = esp_mqtt_client_enqueue(s_client, topic, payload, 0, qos, retain ? 1 : 0, true);
    if (msg_id < 0) {
        s_dropped++;
        if (msg_id == -2) {
            ESP_LOGW(TAG, "Outbox full (%d bytes), dropped %s", esp_mqtt_client_get_outbox_size(s_client), topic);
        } else {
            ESP_LOGW(TAG, "Enqueue failed, dropped %s", topic);
        }
    }
    return msg_id;
}

int hi_mqtt_publish(const char *kind, const char *payload, int qos, bool retain)
{
    char topic[TOPIC_MAX_LEN];
    snprintf(topic, sizeof(topic), "%s/%s/%s", s_prefix, s_device_id, kind);
    return publish_topic(topic, payload, qos, retain);
}

/* Skipped while disconnected on purpose: a retained state queued offline would sit in the outbox and
 * push out the readings that actually need to survive the outage. The LWT already says we are gone. */
static void publish_state(void)
{
    if (s_state_fn == NULL || !s_connected) return;
    cJSON *json = s_state_fn();
    if (json == NULL) return;
    char *payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (payload == NULL) {
        ESP_LOGW(TAG, "State document not serialized: out of memory");
        return;
    }
    publish_topic(s_state_topic, payload, 1, true);
    cJSON_free(payload);
}

static void state_task(void *arg)
{
    for (;;) {
        /* Woken early by hi_mqtt_state_publish_now() or by a fresh connection; otherwise on the period. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(s_state_period_s * 1000));
        publish_state();
    }
}

void hi_mqtt_state_publish_now(void)
{
    if (s_state_task != NULL) xTaskNotifyGive(s_state_task);
}

static void subscribe_one(subscription_t *sub)
{
    if (sub->topic[0] == '\0') {
        snprintf(sub->topic, sizeof(sub->topic), "%s/%s/%s", s_prefix, s_device_id, sub->kind);
    }
    if (esp_mqtt_client_subscribe(s_client, sub->topic, 1) < 0) ESP_LOGW(TAG, "Subscribe to %s failed", sub->topic);
}

esp_err_t hi_mqtt_subscribe(const char *kind, size_t max_len, hi_mqtt_message_fn_t cb)
{
    if (kind == NULL || cb == NULL || max_len == 0 || strlen(kind) >= sizeof(s_subs[0].kind)) return ESP_ERR_INVALID_ARG;
    if (s_sub_count >= HI_MQTT_MAX_SUBSCRIPTIONS) return ESP_ERR_NO_MEM;
    char *buf = malloc(max_len);
    if (buf == NULL) return ESP_ERR_NO_MEM;
    subscription_t *sub = &s_subs[s_sub_count];
    *sub = (subscription_t) {.max_len = max_len, .buf = buf, .cb = cb};
    snprintf(sub->kind, sizeof(sub->kind), "%s", kind);
    s_sub_count++;
    if (s_client != NULL && s_connected) subscribe_one(sub);
    return ESP_OK;
}

/* esp-mqtt delivers a long message in several DATA events; only the first carries the topic. */
static void on_data(const esp_mqtt_event_handle_t event)
{
    if (event->current_data_offset == 0) {
        s_rx_sub = -1;
        for (int i = 0; i < s_sub_count; i++) {
            if (event->topic_len == (int) strlen(s_subs[i].topic) &&
                memcmp(event->topic, s_subs[i].topic, event->topic_len) == 0) {
                s_rx_sub = i;
                break;
            }
        }
        if (s_rx_sub >= 0 && (size_t) event->total_data_len > s_subs[s_rx_sub].max_len) {
            ESP_LOGW(TAG, "Dropped %d-byte message on %s (limit %u)", event->total_data_len,
                     s_subs[s_rx_sub].topic, (unsigned) s_subs[s_rx_sub].max_len);
            s_rx_sub = -1;
        }
    }
    if (s_rx_sub < 0) return;
    subscription_t *sub = &s_subs[s_rx_sub];
    if ((size_t) (event->current_data_offset + event->data_len) > sub->max_len) {
        s_rx_sub = -1;
        return;
    }
    memcpy(sub->buf + event->current_data_offset, event->data, event->data_len);
    if (event->current_data_offset + event->data_len >= event->total_data_len) {
        sub->cb(sub->buf, (size_t) event->total_data_len);
        s_rx_sub = -1;
    }
}

static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    const esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t) event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT connected, %d bytes queued", esp_mqtt_client_get_outbox_size(s_client));
        esp_mqtt_client_enqueue(s_client, s_status_topic, "online", 0, 1, 1, true);
        /* Refresh the retained state so a subscriber never sees one from before the outage. */
        hi_mqtt_state_publish_now();
        for (int i = 0; i < s_sub_count; i++) subscribe_one(&s_subs[i]);
        if (s_on_connection) s_on_connection(true);
        break;
    case MQTT_EVENT_DATA:
        on_data(event);
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        if (s_on_connection) s_on_connection(false);
        break;
    case MQTT_EVENT_ERROR:
        // Warning, not error: an unreachable broker must not turn into error notifications
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGW(TAG, "MQTT connection refused, code %d", event->error_handle->connect_return_code);
        } else {
            ESP_LOGW(TAG, "MQTT error type %d, socket errno %d", event->error_handle->error_type,
                     event->error_handle->esp_transport_sock_errno);
        }
        break;
    default:
        break;
    }
}

esp_err_t hi_mqtt_start(const hi_mqtt_config_t *config)
{
    if (config == NULL || config->broker_uri == NULL || config->topic_prefix == NULL ||
        config->username == NULL || config->client_id_prefix == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->password == NULL || config->password[0] == '\0') {
        ESP_LOGW(TAG, "No MQTT password: client disabled");
        return ESP_ERR_INVALID_ARG;
    }
    if (s_client != NULL) return ESP_ERR_INVALID_STATE;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_prefix, sizeof(s_prefix), "%s", config->topic_prefix);
    snprintf(s_client_id, sizeof(s_client_id), "%s-%s", config->client_id_prefix, s_device_id);
    snprintf(s_status_topic, sizeof(s_status_topic), "%s/%s/status", s_prefix, s_device_id);
    snprintf(s_state_topic, sizeof(s_state_topic), "%s/%s/state", s_prefix, s_device_id);

    s_state_fn = config->state_fn;
    s_on_connection = config->on_connection;
    s_state_period_s = config->state_period_s ? config->state_period_s : DEFAULT_STATE_PERIOD_S;

    const esp_mqtt_client_config_t client_config = {
        .broker.address.uri = config->broker_uri,
        .credentials = {
            .username = config->username,
            .client_id = s_client_id,
            .authentication.password = config->password,
        },
        .session.last_will = {
            .topic = s_status_topic,
            .msg = "offline",
            .qos = 1,
            .retain = 1,
        },
        .outbox.limit = config->outbox_limit_bytes ? config->outbox_limit_bytes : DEFAULT_OUTBOX_LIMIT_BYTES,
        .network.reconnect_timeout_ms = config->reconnect_timeout_ms ? config->reconnect_timeout_ms
                                                                     : DEFAULT_RECONNECT_TIMEOUT_MS,
    };
    s_client = esp_mqtt_client_init(&client_config);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "MQTT disabled: out of memory");
        return ESP_ERR_NO_MEM;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, on_mqtt_event, NULL);
    const esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return err;
    }

    if (s_state_fn != NULL) {
        const uint32_t stack = config->state_stack_size ? config->state_stack_size : DEFAULT_STATE_STACK;
        if (xTaskCreate(state_task, "mqtt_state", stack, NULL, 3, &s_state_task) != pdPASS) {
            ESP_LOGE(TAG, "State task not started: retained state disabled");
            s_state_task = NULL;
            s_state_fn = NULL;
        }
    }
    ESP_LOGI(TAG, "MQTT client %s started, broker %s", s_client_id, config->broker_uri);
    return ESP_OK;
}

void hi_mqtt_stats(hi_mqtt_stats_t *out)
{
    out->enabled = s_client != NULL;
    out->connected = s_connected;
    out->dropped = s_dropped;
    out->outbox_bytes = s_client != NULL ? esp_mqtt_client_get_outbox_size(s_client) : 0;
}
