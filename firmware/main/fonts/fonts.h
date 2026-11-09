/* 自动生成：tools/make_font.py  —— 请勿手改 */
#pragma once

#include <stdint.h>

typedef struct {
    uint16_t cp;      /* 字符码点 */
    uint16_t off;     /* 位图在 blob 中的偏移 */
    uint16_t len;     /* 位图字节数 */
    uint8_t  w;       /* 宽（像素） */
    uint8_t  h;       /* 高（像素） */
    int8_t   xoff;    /* 相对基线的水平偏移 */
    int8_t   yoff;    /* 相对基线的垂直偏移（向上为负） */
    uint8_t  adv;     /* 前进宽度 */
} font_glyph_t;

typedef struct {
    const font_glyph_t *glyphs;
    uint16_t count;
    const uint8_t *blob;
    int8_t ascent;
    int8_t descent;
} bitmap_font_t;

extern const bitmap_font_t font_word;    /* Arial Bold 32，单词 */
extern const bitmap_font_t font_ipa;     /* Arial 16，音标 */
extern const bitmap_font_t font_small;   /* Arial 12，例句 */
extern const bitmap_font_t font_cjk;     /* 微软雅黑 16，释义 */

const font_glyph_t *font_find(const bitmap_font_t *f, uint16_t cp);
