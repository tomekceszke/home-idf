/* Host test: cc -Wall -Wextra -Werror -I include src/hi_wifi_roam.c test/test_wifi_roam.c -o /tmp/t && /tmp/t */
#include <stdio.h>
#include "hi_wifi_roam.h"

static int fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fail = 1; } } while (0)

static const uint8_t AP_A[6] = {2, 0, 0, 0, 0, 0xa};
static const uint8_t AP_B[6] = {2, 0, 0, 0, 0, 0xb};
static const uint8_t AP_C[6] = {2, 0, 0, 0, 0, 0xc};

#define BSS(mac, r) {{mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]}, (r)}

int main(void)
{
    // 2026-11-10: left on a far AP at -86 after the garage AP came back at -55
    hi_wifi_bss_t night[] = {BSS(AP_B, -55), BSS(AP_A, -86), BSS(AP_C, -88)};
    CHECK(hi_wifi_roam_pick(night, 3, AP_A, -86, 10) == 0);

    // Not enough gain: stay
    hi_wifi_bss_t close[] = {BSS(AP_B, -70), BSS(AP_A, -78)};
    CHECK(hi_wifi_roam_pick(close, 2, AP_A, -78, 10) == -1);
    // Exactly the gain: move
    hi_wifi_bss_t edge[] = {BSS(AP_B, -68), BSS(AP_A, -78)};
    CHECK(hi_wifi_roam_pick(edge, 2, AP_A, -78, 10) == 0);

    // The current AP's signal comes from the scan, not the link, when both are there
    hi_wifi_bss_t scan[] = {BSS(AP_B, -72), BSS(AP_A, -76)};
    CHECK(hi_wifi_roam_pick(scan, 2, AP_A, -90, 10) == -1);

    // Current AP missing from the scan: the link RSSI stands in
    hi_wifi_bss_t missing[] = {BSS(AP_B, -60)};
    CHECK(hi_wifi_roam_pick(missing, 1, AP_A, -85, 10) == 0);
    CHECK(hi_wifi_roam_pick(missing, 1, AP_A, -65, 10) == -1);

    // Only the current AP, or nothing at all: stay
    hi_wifi_bss_t alone[] = {BSS(AP_A, -85)};
    CHECK(hi_wifi_roam_pick(alone, 1, AP_A, -85, 10) == -1);
    CHECK(hi_wifi_roam_pick(alone, 0, AP_A, -85, 10) == -1);

    // The strongest other AP wins whatever the order
    hi_wifi_bss_t order[] = {BSS(AP_A, -84), BSS(AP_C, -71), BSS(AP_B, -58)};
    CHECK(hi_wifi_roam_pick(order, 3, AP_A, -84, 10) == 2);

    if (!fail) printf("test_wifi_roam: OK\n");
    return fail;
}
