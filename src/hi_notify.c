#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "hi_notify.h"
#include "hi_notify_late.h"
#include "hi_wifi.h"

static const char *TAG = "HI_NOTIFY";

#define TITLE_SIZE 48
#define MSG_SIZE   160
#define TAGS_SIZE  32
#define NOTE_SIZE  48
#define DEDUP_SLOTS 8
#define SEND_TRIES      3       // ntfy unreachable: tries per notification while connected
#define RETRY_MS        5000
#define WAIT_WIFI_MS    1000    // offline: check this often whether the station is back
#define CLOCK_SET_EPOCH 1700000000  // time() below this: the clock was not set yet (2023-11)

typedef enum { CH_EVENT, CH_WARNING, CH_ERROR, CH_ALARM } channel_t;

typedef struct {
    uint8_t channel;
    uint8_t priority;
    int64_t raised_ms;          // esp_timer, for the delay
    time_t raised_at;           // wall clock, 0 = not set when it was raised
    char title[TITLE_SIZE];
    char message[MSG_SIZE];
    char tags[TAGS_SIZE];
} item_t;

static hi_notify_config_t s_config;
static QueueHandle_t s_queue;
static volatile bool s_error_suppressed;

static struct {
    uint32_t hash;
    int64_t last_sent_ms;
} s_dedup[DEDUP_SLOTS];

static bool empty(const char *s)
{
    return s == NULL || s[0] == '\0';
}

static uint32_t djb2(const char *s)
{
    uint32_t h = 5381;
    int c;
    while ((c = (unsigned char) *s++)) {
        h = h * 33 ^ (uint32_t) c;
    }
    return h;
}

/* Called from the notify task only. */
static bool dedup_should_send(const char *msg)
{
    // Skip an "E (timestamp) " log prefix so the same error at different times hashes identically
    const char *content = strchr(msg, ')');
    content = content && content[1] == ' ' ? content + 2 : msg;
    uint32_t h = djb2(content);
    int64_t now_ms = esp_timer_get_time() / 1000;
    int oldest = 0;
    for (int i = 0; i < DEDUP_SLOTS; i++) {
        if (s_dedup[i].hash == h && s_dedup[i].last_sent_ms != 0) {
            if (now_ms - s_dedup[i].last_sent_ms < (int64_t) s_config.error_cooldown_s * 1000) return false;
            s_dedup[i].last_sent_ms = now_ms;
            return true;
        }
        if (s_dedup[i].last_sent_ms < s_dedup[oldest].last_sent_ms) oldest = i;
    }
    s_dedup[oldest].hash = h;
    s_dedup[oldest].last_sent_ms = now_ms;
    return true;
}

/* True when ntfy took it (HTTP 2xx). body: the message, with the late note if any. */
static bool post_ntfy(const char *topic, const item_t *item, const char *body)
{
    bool ok = false;
    char url[128];
    snprintf(url, sizeof(url), "%s/%s", s_config.server ? s_config.server : "https://ntfy.sh", topic);
    esp_http_client_config_t http = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http);
    if (client == NULL) return false;
    esp_http_client_set_header(client, "Title", item->title);
    if (item->priority != HI_NOTIFY_PRIO_DEFAULT) {
        char prio[2] = {(char) ('0' + item->priority), '\0'};
        esp_http_client_set_header(client, "Priority", prio);
    }
    if (item->tags[0] != '\0') esp_http_client_set_header(client, "Tags", item->tags);
    if (!empty(s_config.click_url)) esp_http_client_set_header(client, "Click", s_config.click_url);
    esp_http_client_set_post_field(client, body, (int) strlen(body));
    esp_err_t err = esp_http_client_perform(client);
    const int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    ok = status >= 200 && status < 300;
    if (!ok) {
        // Warning, not error: an error line would be forwarded to notifications again
        ESP_LOGW(TAG, "Sending failed: %s, HTTP %d", esp_err_to_name(err), status);
    }
    esp_http_client_cleanup(client);
    return ok;
}

static void wait_for_wifi(void)
{
    while (!hi_wifi_is_connected()) vTaskDelay(pdMS_TO_TICKS(WAIT_WIFI_MS));
}

/* A notification is held while the station is offline and sent once it is back, with a note of when it was raised
 * (hi_notify_late.h): an error raised by the outage itself used to be dropped (heatronic-controller, 2026-11-18).
 * Up to SEND_TRIES tries while connected. Meanwhile new ones wait in the queue; a full queue drops the newest. */
static void notify_task(void *arg)
{
    item_t item;
    char body[MSG_SIZE + NOTE_SIZE];
    for (;;) {
        if (xQueueReceive(s_queue, &item, portMAX_DELAY) != pdTRUE) continue;
        const char *topic;
        switch ((channel_t) item.channel) {
        case CH_ERROR:
            if (!dedup_should_send(item.message)) continue;
            topic = s_config.error_topic;
            break;
        case CH_ALARM:
            topic = empty(s_config.error_topic) ? s_config.topic : s_config.error_topic;
            break;
        case CH_WARNING:
            topic = empty(s_config.warning_topic) ? s_config.topic : s_config.warning_topic;
            break;
        default:
            topic = s_config.topic;
            break;
        }
        for (int attempt = 0; attempt < SEND_TRIES; attempt++) {
            if (attempt) vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
            wait_for_wifi();
            struct tm raised;
            const bool clock = item.raised_at != 0 && localtime_r(&item.raised_at, &raised) != NULL;
            const int64_t age_s = (esp_timer_get_time() / 1000 - item.raised_ms) / 1000;
            char note[NOTE_SIZE];
            hi_notify_late_note(note, sizeof(note), age_s, clock ? &raised : NULL);
            snprintf(body, sizeof(body), "%s%s", item.message, note);
            if (post_ntfy(topic, &item, body)) break;
        }
    }
}

void hi_notify_init(const hi_notify_config_t *config)
{
    s_config = *config;
    if (s_config.error_title == NULL) s_config.error_title = "Device error";
    if (empty(s_config.topic) && empty(s_config.warning_topic) && empty(s_config.error_topic)) return;
    s_queue = xQueueCreate(s_config.queue_size ? s_config.queue_size : 6, sizeof(item_t));
    if (s_queue == NULL
        || xTaskCreate(notify_task, "hi_notify", CONFIG_HOME_IDF_NOTIFY_TASK_STACK, NULL, 3, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Notifications disabled: out of memory");
        s_queue = NULL;
    }
}

static void enqueue(channel_t channel, const char *title, const char *message, uint8_t priority, const char *tags)
{
    if (s_queue == NULL) return;
    const time_t now = time(NULL);
    item_t item = {.channel = (uint8_t) channel, .priority = priority, .raised_ms = esp_timer_get_time() / 1000,
                   .raised_at = now >= CLOCK_SET_EPOCH ? now : 0};
    snprintf(item.title, sizeof(item.title), "%s", title);
    snprintf(item.message, sizeof(item.message), "%s", message);
    snprintf(item.tags, sizeof(item.tags), "%s", tags ? tags : "");
    xQueueSend(s_queue, &item, 0);      // drop when full, never block the caller
}

void hi_notify_event(const char *title, const char *message)
{
    hi_notify_event_ex(title, message, HI_NOTIFY_PRIO_DEFAULT, NULL);
}

void hi_notify_event_ex(const char *title, const char *message, hi_notify_priority_t priority, const char *tags)
{
    if (empty(s_config.topic)) return;
    enqueue(CH_EVENT, title, message, (uint8_t) priority, tags);
}

void hi_notify_warning_ex(const char *title, const char *message, hi_notify_priority_t priority, const char *tags)
{
    if (empty(s_config.warning_topic) && empty(s_config.topic)) return;
    enqueue(CH_WARNING, title, message, (uint8_t) priority, tags);
}

void hi_notify_alarm_ex(const char *title, const char *message, hi_notify_priority_t priority, const char *tags)
{
    if (empty(s_config.error_topic) && empty(s_config.topic)) return;
    enqueue(CH_ALARM, title, message, (uint8_t) priority, tags);
}

void hi_notify_error(const char *message)
{
    if (empty(s_config.error_topic) || s_error_suppressed) return;
    enqueue(CH_ERROR, s_config.error_title, message, HI_NOTIFY_PRIO_HIGH, "warning");
}

void hi_notify_error_suppress(bool suppress)
{
    s_error_suppressed = suppress;
}
