/*******************************************************************************
 * Size: 28 px
 * Bpp: 4
 * Opts: --font nunito-400.ttf --symbols +− --size 28 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h --lv-font-name hi_font_nunito_28 -o hi_font_nunito_28.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef HI_FONT_NUNITO_28
#define HI_FONT_NUNITO_28 1
#endif

#if HI_FONT_NUNITO_28

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+002B "+" */
    0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x6, 0xf3, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x8f, 0x40, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x8, 0xf4, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x8f, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x8, 0xf4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x8f, 0x40, 0x0, 0x0, 0x7, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xf4, 0x5c, 0xcc, 0xcc, 0xef,
    0xdc, 0xcc, 0xcb, 0x30, 0x0, 0x0, 0x8, 0xf4,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8f, 0x40,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0xf4, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x8f, 0x40, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x8, 0xf4, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x5f, 0x20, 0x0, 0x0,
    0x0,

    /* U+2212 "−" */
    0x9f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x27,
    0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xb1
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 269, .box_w = 15, .box_h = 15, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 113, .adv_w = 269, .box_w = 15, .box_h = 2, .ofs_x = 1, .ofs_y = 7}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x21e7
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 43, .range_length = 8680, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 2, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
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
const lv_font_t hi_font_nunito_28 = {
#else
lv_font_t hi_font_nunito_28 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 15,          /*The maximum line height required by the font*/
    .base_line = -1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if HI_FONT_NUNITO_28*/

