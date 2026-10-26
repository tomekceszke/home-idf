/* Host test of the generated design header:
 *   python3 tools/design_tokens.py header thermostat /tmp/d/hi_design_thermostat.h
 *   cc -Wall -Wextra -Werror -I /tmp/d test/test_design.c -o /tmp/t && /tmp/t */
#include <stdio.h>
#include <string.h>
#include "hi_design_thermostat.h"

static int fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fail = 1; } } while (0)
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const hi_thermostat_state_t *state(const char *key)
{
    for (size_t i = 0; i < COUNT(HI_THERMOSTAT_STATES); i++) {
        if (strcmp(HI_THERMOSTAT_STATES[i].key, key) == 0) return &HI_THERMOSTAT_STATES[i];
    }
    return NULL;
}

int main(void)
{
    // Every state of the thermostat document (heatronic docs/DATA.md) has an entry, plus the flame split of heat
    const char *keys[] = {"heat_flame", "heat", "dhw", "idle", "fault", "unknown"};
    for (size_t i = 0; i < COUNT(keys); i++) CHECK(state(keys[i]) != NULL);
    CHECK(COUNT(HI_THERMOSTAT_STATES) == COUNT(keys));

    // Colours resolve through the tokens: the heating arc is the view's heat colour, faults draw the dim arc
    CHECK(state("heat_flame")->arc == HI_COLOR_HEAT);
    CHECK(state("fault")->arc == HI_COLOR_FAULT_ARC);
    CHECK(state("fault")->icon_color == HI_COLOR_BAD);

    // Polish text survives as UTF-8 ("ł" = c5 82) and icons are 3-byte private-use characters
    CHECK(strcmp(state("dhw")->label, "Ciep\xc5\x82" "a woda") == 0);
    CHECK(strlen(HI_ICON_FLAME) == 3 && (unsigned char) HI_ICON_FLAME[0] == 0xee);
    CHECK(strcmp(HI_THERMOSTAT_MISSING, "\xe2\x80\x93") == 0);

    // The dial geometry the page and the panel share
    CHECK(HI_THERMOSTAT_MIN_C == 15 && HI_THERMOSTAT_MAX_C == 25);
    CHECK(HI_THERMOSTAT_DIAL_SPAN_DEG == 270 && HI_THERMOSTAT_DIAL_BASE == 2 * HI_THERMOSTAT_DIAL_CENTER);
    CHECK(HI_THERMOSTAT_PANEL_DIAL_X * 2 + HI_THERMOSTAT_PANEL_DIAL_SIZE == HI_THERMOSTAT_PANEL_WIDTH);

    CHECK(COUNT(HI_THERMOSTAT_MODES) == 5);
    CHECK(strcmp(HI_THERMOSTAT_SOURCES[1][0], "w100") == 0);

    // The panel's state line: label colour and the muted detail; days start on Sunday (struct tm tm_wday)
    CHECK(state("idle")->label_color == HI_COLOR_TEXT && state("idle")->detail[0] == '\0');
    CHECK(strcmp(state("heat_flame")->detail, "\xc2\xb7 palnik {0} %") == 0);
    CHECK(COUNT(HI_THERMOSTAT_TEXT_DAYS) == 7 && strcmp(HI_THERMOSTAT_TEXT_DAYS[1], "pn") == 0);

    if (!fail) printf("design: all checks passed\n");
    return fail;
}
