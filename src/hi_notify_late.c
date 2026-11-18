#include "hi_notify_late.h"

#include <stdio.h>

size_t hi_notify_late_note(char *out, size_t size, int64_t age_s, const struct tm *raised)
{
    if (size == 0) return 0;
    out[0] = '\0';
    if (age_s < HI_NOTIFY_LATE_S) return 0;
    char delay[24];
    if (age_s < 120) snprintf(delay, sizeof(delay), "%d s", (int) age_s);
    else if (age_s < 120 * 60) snprintf(delay, sizeof(delay), "%d min", (int) (age_s / 60));
    else snprintf(delay, sizeof(delay), "%d h", (int) (age_s / 3600));
    int n;
    if (raised) {
        n = snprintf(out, size, " [raised %02d:%02d:%02d, sent %s late]", raised->tm_hour, raised->tm_min,
                     raised->tm_sec, delay);
    } else {
        n = snprintf(out, size, " [sent %s late]", delay);
    }
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t) n < size ? (size_t) n : size - 1;
}
