#pragma once

#include "fonts.h"

/* 逻辑画布：横屏 296x128，坐标原点在左上角。
 * 绘制时自动旋转到面板的 128x296 原生坐标。 */
#define GFX_W   296
#define GFX_H   128

void gfx_clear(int color);
void gfx_pixel(int x, int y, int color);
void gfx_fill(int x, int y, int w, int h, int color);
void gfx_hline(int x0, int x1, int y, int color);

/* 以基线为基准绘制 UTF-8 文本 */
void gfx_text(const bitmap_font_t *f, int x, int baseline, const char *utf8, int color);
int  gfx_text_width(const bitmap_font_t *f, const char *utf8);
