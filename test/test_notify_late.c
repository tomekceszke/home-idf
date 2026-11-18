/* Host test of hi_notify_late_note: cc -Wall -Wextra -Werror -I include src/hi_notify_late.c test/test_notify_late.c */
#include <stdio.h>
#include <string.h>

#include "hi_notify_late.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void)
{
    char out[64];
    const struct tm t = {.tm_hour = 11, .tm_min = 59, .tm_sec = 17};

    CHECK(hi_notify_late_note(out, sizeof(out), 0, &t) == 0 && out[0] == '\0');
    CHECK(hi_notify_late_note(out, sizeof(out), HI_NOTIFY_LATE_S - 1, &t) == 0 && out[0] == '\0');

    hi_notify_late_note(out, sizeof(out), 30, &t);
    CHECK(strcmp(out, " [raised 11:59:17, sent 30 s late]") == 0);
    hi_notify_late_note(out, sizeof(out), 125, &t);
    CHECK(strcmp(out, " [raised 11:59:17, sent 2 min late]") == 0);
    hi_notify_late_note(out, sizeof(out), 3 * 3600 + 5, &t);
    CHECK(strcmp(out, " [raised 11:59:17, sent 3 h late]") == 0);

    /* no clock when it was raised: the delay only */
    hi_notify_late_note(out, sizeof(out), 45, NULL);
    CHECK(strcmp(out, " [sent 45 s late]") == 0);

    /* a short buffer is cut, never overrun */
    char small[8];
    const size_t n = hi_notify_late_note(small, sizeof(small), 45, &t);
    CHECK(n == sizeof(small) - 1 && small[sizeof(small) - 1] == '\0');
    CHECK(hi_notify_late_note(small, 0, 45, &t) == 0);

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("notify_late: all tests passed\n");
    return 0;
}
