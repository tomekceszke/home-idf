#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HI_NOTIFY_PRIO_MIN = 1,
    HI_NOTIFY_PRIO_LOW = 2,
    HI_NOTIFY_PRIO_DEFAULT = 3,
    HI_NOTIFY_PRIO_HIGH = 4,
    HI_NOTIFY_PRIO_URGENT = 5,
} hi_notify_priority_t;

typedef struct {
    const char *server;             // NULL = "https://ntfy.sh"
    const char *topic;              // events; "" or NULL disables
    const char *error_topic;        // errors; "" or NULL disables
    const char *error_title;        // e.g. "Water controller error"
    const char *click_url;          // optional: opened when the notification is tapped
    uint32_t error_cooldown_s;      // same error text is sent at most once per window
    uint8_t queue_size;             // 0 = 6
} hi_notify_config_t;

/* Background task + queue. Senders never block: items are dropped when the queue is full or WiFi is down. */
void hi_notify_init(const hi_notify_config_t *config);

void hi_notify_event(const char *title, const char *message);
/* tags: comma-separated ntfy tags/emoji shortcodes, may be NULL. */
void hi_notify_event_ex(const char *title, const char *message, hi_notify_priority_t priority, const char *tags);

void hi_notify_error(const char *message);
/* Temporarily mute errors (e.g. expected failures during OTA). */
void hi_notify_error_suppress(bool suppress);
