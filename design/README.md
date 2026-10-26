# design

One look for every screen of the controllers: the app pages (phone and desktop, HTML/CSS/SVG in the shared shell)
and the wall panels (LVGL). CSS cannot reach a panel, so what is shared is data, and each renderer is generated
from it instead of copying values by hand.

| Path | What |
|---|---|
| `tokens.json` | the family: palette, sizes, fonts. Equal to the `:root` of `web/app_shell.css` (`tools/design_tokens.py check`, in CI) |
| `views/<view>/view.json` | one view: its colours, geometry, states and modes with their labels (English and Polish), icons, and a section per renderer (`panel` = 480×480 layout, text sizes, LVGL fonts) |
| `views/<view>/mockup/` | the accepted mockup the view is drawn from |
| `fonts/` | licences of the fonts; `generated/web` woff2 subsets for pages, `generated/lvgl` LVGL fonts for panels |

Shared through the data: colours by state, the dial (range, 270° arc, step, ticks, text offsets at a base size),
labels and icons of states and modes, how a missing or stale value looks. Per renderer: layout and sizes,
interaction (sign-in, tabs and settings on a page; touch to wake, swipe and night dimming on a panel), and the
logic that maps the device's JSON onto the tables, which stays small because the tables come from here.

## Views

| View | Data | Used by |
|---|---|---|
| `thermostat` | `heatronic/<mac>/thermostat` (heatronic-controller `docs/DATA.md`) | wall-panel (LVGL); heatronic-controller's app page next |

## Fonts

Text is **Nunito** (SIL OFL 1.1, `fonts/Nunito-OFL.txt`), icons are **Material Symbols Rounded**, filled
(Apache-2.0, `fonts/MaterialSymbols-LICENSE.txt`). Both come from pinned upstream commits with sha256 checks
(`tools/make_fonts.py`). The generated files are committed, so building firmware needs neither Node nor fonttools;
run `tools/make_fonts.py` (uvx + npx) after changing a view's `panel.fonts`, `glyph_sets` or `icons`.

## Use

Panel (`main/CMakeLists.txt`, the component requires lvgl):

```cmake
home_idf_design(${COMPONENT_LIB} VIEW thermostat LVGL_FONTS)
```

```c
#include "hi_design_thermostat.h"   // HI_COLOR_*, HI_THERMOSTAT_*, HI_ICON_*, state / mode tables
#include "hi_design_fonts.h"        // hi_font_nunito_<size>, hi_font_nunito_semibold_<size>
lv_obj_set_style_text_font(label, &hi_font_nunito_semibold_84, 0);
lv_obj_set_style_text_color(label, lv_color_hex(HI_COLOR_TEXT), 0);
```

Page: `tools/design_tokens.py css <view> out.css` gives the `:root` block with the family's and the view's colours;
the woff2 files are for `@font-face`.
