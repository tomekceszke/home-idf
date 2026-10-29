/* Host test of the generated weather header:
 *   python3 tools/design_tokens.py header weather /tmp/d/hi_design_weather.h
 *   cc -Wall -Wextra -Werror -I /tmp/d test/test_design_weather.c -o /tmp/t && /tmp/t */
#include <stdio.h>
#include <string.h>
#include "hi_design_weather.h"

static int fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fail = 1; } } while (0)
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

int main(void)
{
    // Every WMO weather_code Open-Meteo documents has exactly one row, with icons and words
    static const int codes[] = {0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 71, 73, 75, 77, 80, 81, 82,
                                85, 86, 95, 96, 99};
    for (size_t i = 0; i < COUNT(codes); i++) {
        int rows = 0;
        for (size_t j = 0; j < COUNT(HI_WEATHER_CONDITIONS); j++) {
            const hi_weather_condition_t *c = &HI_WEATHER_CONDITIONS[j];
            if (c->code != codes[i]) continue;
            rows++;
            CHECK(c->icon_day[0] != '\0' && c->icon_night[0] != '\0' && c->label[0] != '\0');
        }
        if (rows != 1) printf("code %d: %d rows\n", codes[i], rows);
        CHECK(rows == 1);
    }
    CHECK(COUNT(HI_WEATHER_CONDITIONS) == COUNT(codes));

    // Colours resolve through the tokens; clear night has its own icon
    CHECK(HI_WEATHER_CONDITIONS[0].code == 0 && HI_WEATHER_CONDITIONS[0].color == HI_COLOR_SUN);
    CHECK(strcmp(HI_WEATHER_CONDITIONS[0].icon_day, HI_WEATHER_CONDITIONS[0].icon_night) != 0);
    CHECK(HI_WEATHER_PANEL_SEP_COLOR == HI_COLOR_SEP);
    CHECK(strcmp(HI_WEATHER_TEXT_DAYS[3], "\305\233roda") == 0 && COUNT(HI_WEATHER_TEXT_MONTHS) == 12);
    CHECK(strstr(HI_WEATHER_TEXT_FORECAST_AGE, "{0}") != NULL);
    CHECK(HI_WEATHER_PANEL_BIG_SIZE_SMALL < HI_WEATHER_PANEL_BIG_SIZE);

    if (!fail) printf("design weather: all checks passed\n");
    return fail;
}
