#pragma once

/*
 * The note hi_notify appends to a notification it could not send at once (no WiFi, or ntfy unreachable) and sent
 * later: when it was raised and how late it goes out. Pure C (no ESP-IDF), host-tested in test/test_notify_late.c.
 */

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define HI_NOTIFY_LATE_S 30     // sent this long after it was raised or later: the note is added

/* Writes the note into out (always terminated) and returns its length; "" (0) when age_s < HI_NOTIFY_LATE_S.
 * raised: the local time it was raised, NULL when the clock was not set then (the note gives the delay only).
 * Example: " [raised 11:59:17, sent 2 min late]". */
size_t hi_notify_late_note(char *out, size_t size, int64_t age_s, const struct tm *raised);
