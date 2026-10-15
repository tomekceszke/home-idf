/* Host test: cc -Wall -Wextra -Werror -I include src/hi_deadband.c test/test_deadband.c -o /tmp/t && /tmp/t */
#include <stdio.h>
#include "hi_deadband.h"

static int fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fail = 1; } } while (0)

int main(void)
{
    hi_deadband_t d = {0};

    // The first reading always goes out
    CHECK(hi_deadband_due(&d, 30.0f, 100, 0.2f, 55));

    // DS18B20 steps are 0.0625: three steps stay below 0.2, four reach it (up or down from the last one sent)
    CHECK(!hi_deadband_due(&d, 30.0625f, 105, 0.2f, 55));
    CHECK(!hi_deadband_due(&d, 30.1875f, 110, 0.2f, 55));
    CHECK(!hi_deadband_due(&d, 29.8125f, 115, 0.2f, 55));
    CHECK(hi_deadband_due(&d, 30.25f, 120, 0.2f, 55));
    CHECK(!hi_deadband_due(&d, 30.0625f, 125, 0.2f, 55));      // measured from 30.25 now, not from 30.0
    CHECK(hi_deadband_due(&d, 30.0f, 130, 0.2f, 55));

    // A quiet sensor still goes out once max_gap_s after the last send
    CHECK(!hi_deadband_due(&d, 30.0f, 184, 0.2f, 55));
    CHECK(hi_deadband_due(&d, 30.0f, 185, 0.2f, 55));
    CHECK(!hi_deadband_due(&d, 30.0f, 190, 0.2f, 55));         // and the gap restarts there

    // After a reset (sensor lost, then back) the next reading goes out whatever its value
    hi_deadband_reset(&d);
    CHECK(hi_deadband_due(&d, 30.0f, 195, 0.2f, 55));

    // Monotonic counters that wrap still see the gap
    hi_deadband_t w = {0};
    CHECK(hi_deadband_due(&w, 20.0f, 0xFFFFFFF0u, 0.2f, 55));
    CHECK(!hi_deadband_due(&w, 20.0f, 0x00000010u, 0.2f, 55));  // 32 s later
    CHECK(hi_deadband_due(&w, 20.0f, 0x00000027u, 0.2f, 55));   // 55 s later

    printf("%s\n", fail ? "FAIL" : "ok");
    return fail;
}
