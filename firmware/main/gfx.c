/*
 * 逻辑画布（横屏 296x128）与文字绘制
 *
 * 面板原生是 128x296 竖屏，这里做 90° 旋转：
 *   逻辑 (lx, ly)  ->  面板 (127 - ly, lx)
 */
#include <stdint.h>
#include <string.h>

#include "epd.h"
#include "gfx.h"

static inline void put(int lx, int ly, int color)
{
    if (lx < 0 || lx >= GFX_W || ly < 0 || ly >= GFX_H) return;
    epd_set_pixel(EPD_WIDTH - 1 - ly, lx, color);
}

void gfx_clear(int color)
{
    epd_clear(color);
}

void gfx_pixel(int x, int y, int color)
{
    put(x, y, color);
}

void gfx_fill(int x, int y, int w, int h, int color)
{
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            put(i, j, color);
        }
    }
}

void gfx_hline(int x0, int x1, int y, int color)
{
    for (int x = x0; x <= x1; x++) put(x, y, color);
}

/* UTF-8 解码：返回码点，并推进指针 */
static uint32_t utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp;
    int extra;

    if (s[0] < 0x80)      { cp = s[0];        extra = 0; }
    else if ((s[0] & 0xE0) == 0xC0) { cp = s[0] & 0x1F; extra = 1; }
    else if ((s[0] & 0xF0) == 0xE0) { cp = s[0] & 0x0F; extra = 2; }
    else if ((s[0] & 0xF8) == 0xF0) { cp = s[0] & 0x07; extra = 3; }
    else { *p += 1; return 0xFFFD; }

    for (int i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) { *p += 1; return 0xFFFD; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *p += extra + 1;
    return cp;
}

const font_glyph_t *font_find(const bitmap_font_t *f, uint16_t cp)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->glyphs[mid].cp == cp) return &f->glyphs[mid];
        if (f->glyphs[mid].cp < cp)  lo = mid + 1;
        else                         hi = mid - 1;
    }
    return NULL;
}

void gfx_text(const bitmap_font_t *f, int x, int baseline, const char *utf8, int color)
{
    const char *p = utf8;
    int pen = x;

    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (cp == 0) break;

        const font_glyph_t *g = font_find(f, (uint16_t)cp);
        if (!g) {
            pen += 6;                 /* 缺字时留一点空隙 */
            continue;
        }

        int row_bytes = (g->w + 7) / 8;
        for (int row = 0; row < g->h; row++) {
            for (int col = 0; col < g->w; col++) {
                uint8_t byte = f->blob[g->off + row * row_bytes + (col >> 3)];
                if (byte & (0x80 >> (col & 7))) {
                    put(pen + g->xoff + col, baseline + g->yoff + row, color);
                }
            }
        }
        pen += g->adv;
    }
}

int gfx_text_width(const bitmap_font_t *f, const char *utf8)
{
    const char *p = utf8;
    int width = 0;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        const font_glyph_t *g = font_find(f, (uint16_t)cp);
        width += g ? g->adv : 6;
    }
    return width;
}
