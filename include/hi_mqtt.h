#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"

/*
 * MQTT client shared by the controllers. Topics are always <prefix>/<mac>/<kind>, where <mac> is the
 * STA MAC as 12 lowercase hex characters, so a subscriber can discover every device with <prefix>/+/<kind>.
 *
 * Two topics are published here rather than by the application:
 *   <prefix>/<mac>/status    "online" / "offline", retained, also the LWT
 *   <prefix>/<mac>/state     the full status JSON, retained, when state_fn is set
 * The retained state is what lets a subscriber that has just connected know the device without asking it
 * anything: it arrives immediately, before the next periodic publish.
 */

typedef struct {
    bool enabled;
    bool connected;
    uint32_t dropped;               // messages the outbox refused
    int outbox_bytes;
} hi_mqtt_stats_t;

/* Builds the retained state document. Called from the state task, never from the caller's context, and
 * must therefore be safe to run while an HTTP handler is serving the same data. Ownership of the returned
 * object passes to hi_mqtt; NULL skips this round. */
typedef cJSON *(*hi_mqtt_state_fn_t)(void);

/* Connection transitions, for applications that record them as their own events. Runs in the MQTT task:
 * do not block and do not publish from here. */
typedef void (*hi_mqtt_connection_fn_t)(bool connected);

typedef struct {
    const char *broker_uri;         // required, e.g. "mqtt://192.168.11.16:1883"
    const char *topic_prefix;       // required, e.g. "heating"
    const char *username;           // required
    const char *password;           // required; NULL or "" leaves MQTT disabled
    const char *client_id_prefix;   // required, e.g. "hc" -> client id "hc-<mac>"
    int outbox_limit_bytes;         // 0 = 24576
    int reconnect_timeout_ms;       // 0 = 30000
    hi_mqtt_state_fn_t state_fn;    // NULL = no state topic and no state task
    uint32_t state_period_s;        // 0 = 60; only with state_fn
    uint32_t state_stack_size;      // 0 = 4096; only with state_fn
    hi_mqtt_connection_fn_t on_connection;  // optional
} hi_mqtt_config_t;

/* Starts the client (and the state task, if configured). Returns ESP_ERR_INVALID_ARG without a password,
 * which is how a device with no broker credentials stays quiet instead of failing to boot. */
esp_err_t hi_mqtt_start(const hi_mqtt_config_t *config);

bool hi_mqtt_enabled(void);

/*
 * Publishes to <prefix>/<mac>/<kind>. Returns the esp-mqtt message id, -2 when the outbox is full, or
 * -1 on any other failure; applications that report a full outbox check for -2.
 *
 * Call only from a task that may block. esp_mqtt_client_enqueue() takes the esp-mqtt API lock with no
 * timeout and the client task holds that lock across a connect attempt, so publishing from a sensor or
 * timer task stalls it for the whole network timeout. That is how heating-controller-1 reset on the task
 * watchdog on 2026-09-20. Producers hand work to a queue; one task does the talking.
 */
int hi_mqtt_publish(const char *kind, const char *payload, int qos, bool retain);

/* Republishes the retained state now instead of waiting for the next period, e.g. after a valve moves.
 * Only signals the state task, so it is safe to call from anywhere, including an HTTP handler. */
void hi_mqtt_state_publish_now(void);

/* The STA MAC as 12 lowercase hex characters. Valid once hi_mqtt_start() has run. */
const char *hi_mqtt_device_id(void);

void hi_mqtt_stats(hi_mqtt_stats_t *out);
