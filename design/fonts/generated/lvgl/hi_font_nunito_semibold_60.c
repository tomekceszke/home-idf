/*******************************************************************************
 * Size: 60 px
 * Bpp: 4
 * Opts: --font nunito-600.ttf --symbols ° --size 60 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h --lv-font-name hi_font_nunito_semibold_60 -o hi_font_nunito_semibold_60.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef HI_FONT_NUNITO_SEMIBOLD_60
#define HI_FONT_NUNITO_SEMIBOLD_60 1
#endif

#if HI_FONT_NUNITO_SEMIBOLD_60

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+00B0 "°" */
    0x0, 0x0, 0x4, 0x9d, 0xff, 0xec, 0x71, 0x0,
    0x0, 0x0, 0x0, 0x2b, 0xff, 0xff, 0xff, 0xff,
    0xf6, 0x0, 0x0, 0x0, 0x3e, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xfa, 0x0, 0x0, 0x2e, 0xff, 0xff,
    0xfc, 0xce, 0xff, 0xff, 0xf8, 0x0, 0xc, 0xff,
    0xff, 0x70, 0x0, 0x3, 0xdf, 0xff, 0xf3, 0x3,
    0xff, 0xff, 0x50, 0x0, 0x0, 0x0, 0xdf, 0xff,
    0xb0, 0x9f, 0xff, 0xa0, 0x0, 0x0, 0x0, 0x3,
    0xff, 0xff, 0xc, 0xff, 0xf5, 0x0, 0x0, 0x0,
    0x0, 0xd, 0xff, 0xf3, 0xef, 0xff, 0x20, 0x0,
    0x0, 0x0, 0x0, 0xbf, 0xff, 0x5e, 0xff, 0xf2,
    0x0, 0x0, 0x0, 0x0, 0xb, 0xff, 0xf5, 0xcf,
    0xff, 0x50, 0x0, 0x0, 0x0, 0x0, 0xdf, 0xff,
    0x39, 0xff, 0xfa, 0x0, 0x0, 0x0, 0x0, 0x3f,
    0xff, 0xf0, 0x3f, 0xff, 0xf5, 0x0, 0x0, 0x0,
    0xd, 0xff, 0xfa, 0x0, 0xbf, 0xff, 0xf7, 0x0,
    0x0, 0x3c, 0xff, 0xff, 0x30, 0x2, 0xef, 0xff,
    0xff, 0xcb, 0xdf, 0xff, 0xff, 0x80, 0x0, 0x3,
    0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xa0, 0x0,
    0x0, 0x2, 0xbf, 0xff, 0xff, 0xff, 0xff, 0x60,
    0x0, 0x0, 0x0, 0x0, 0x49, 0xdf, 0xfe, 0xc7,
    0x10, 0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 360, .box_w = 19, .box_h = 18, .ofs_x = 2, .ofs_y = 25}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 176, .range_length = 1, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t hi_font_nunito_semibold_60 = {
#else
lv_font_t hi_font_nunito_semibold_60 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 18,          /*The maximum line height required by the font*/
    .base_line = -25,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -5,
    .underline_thickness = 2,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if HI_FONT_NUNITO_SEMIBOLD_60*/

