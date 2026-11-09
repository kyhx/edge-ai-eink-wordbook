#!/usr/bin/env python3
"""从 data/vocabulary.csv 生成点阵字模（只包含实际用到的字符）。

输出到 firmware/main/fonts/：
    fonts.h          结构体与字体声明
    font_word.c      单词     Arial Bold 32
    font_ipa.c       音标     Arial 16
    font_small.c     例句     Arial 12
    font_cjk.c       释义     微软雅黑 16

用法： python tools/make_font.py
"""
import csv
import os
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV_PATH = os.path.join(ROOT, "data", "vocabulary.csv")
OUT_DIR = os.path.join(ROOT, "firmware", "main", "fonts")

FONT_WORD = "C:/Windows/Fonts/arialbd.ttf"
FONT_LATIN = "C:/Windows/Fonts/arial.ttf"
FONT_CJK = "C:/Windows/Fonts/msyh.ttc"

THRESHOLD = 110


def load_rows():
    with open(CSV_PATH, encoding="utf-8") as f:
        return list(csv.DictReader(f))


def collect_chars(rows):
    latin = set()
    for r in rows:
        latin |= set(r["word"]) | set(r["example"]) | set(r["phonetic"])
    # 补上界面常用字符
    latin |= set("0123456789 /:%-.!?,'\"")
    latin = {c for c in latin if ord(c) < 0x250}
    cjk = set()
    for r in rows:
        cjk |= set(r["meaning"])
    return sorted(latin), sorted(cjk)


def render(font_path, size_px, chars):
    font = ImageFont.truetype(font_path, size_px)
    ascent, descent = font.getmetrics()
    glyphs = []
    blob = bytearray()

    for ch in chars:
        adv = int(round(font.getlength(ch)))
        bbox = font.getbbox(ch, anchor="ls")
        if bbox is None or bbox[2] - bbox[0] <= 0 or bbox[3] - bbox[1] <= 0:
            glyphs.append((ord(ch), len(blob), 0, 0, 0, 0, 0, adv))
            continue

        x0, y0, x1, y1 = bbox
        w, h = x1 - x0, y1 - y0

        img = Image.new("L", (w, h), 0)
        draw = ImageDraw.Draw(img)
        draw.text((-x0, -y0), ch, font=font, fill=255, anchor="ls")

        row_bytes = (w + 7) // 8
        off = len(blob)
        px = img.load()
        for yy in range(h):
            for bx in range(row_bytes):
                byte = 0
                for bit in range(8):
                    xx = bx * 8 + bit
                    if xx < w and px[xx, yy] >= THRESHOLD:
                        byte |= 0x80 >> bit
                blob.append(byte)

        glyphs.append((ord(ch), off, row_bytes * h, w, h, x0, y0, adv))

    return ascent, descent, glyphs, bytes(blob)


def emit_c(path, ident, font_name, size_px, ascent, descent, glyphs, blob):
    lines = []
    lines.append("/* 自动生成：tools/make_font.py  —— 请勿手改 */")
    lines.append(f"/* {font_name} {size_px}px，{len(glyphs)} 个字形，位图 {len(blob)} 字节 */")
    lines.append('#include "fonts.h"')
    lines.append("")

    if blob:
        lines.append("static const uint8_t %s_blob[] = {" % ident)
        for i in range(0, len(blob), 16):
            chunk = ", ".join("0x%02X" % b for b in blob[i:i + 16])
            lines.append("    " + chunk + ",")
        lines.append("};")
        lines.append("")
    else:
        lines.append("static const uint8_t %s_blob[1] = {0};" % ident)
        lines.append("")

    lines.append("static const font_glyph_t %s_glyphs[] = {" % ident)
    for cp, off, length, w, h, xoff, yoff, adv in glyphs:
        lines.append("    {0x%04X, %d, %d, %d, %d, %d, %d, %d},"
                     % (cp, off, length, w, h, xoff, yoff, adv))
    lines.append("};")
    lines.append("")

    lines.append("const bitmap_font_t %s = {" % ident)
    lines.append("    .glyphs  = %s_glyphs," % ident)
    lines.append("    .count   = %d," % len(glyphs))
    lines.append("    .blob    = %s_blob," % ident)
    lines.append("    .ascent  = %d," % ascent)
    lines.append("    .descent = %d," % descent)
    lines.append("};")
    lines.append("")

    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


HEADER = """/* 自动生成：tools/make_font.py  —— 请勿手改 */
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
"""


def emit_vocab(rows):
    """把词库也生成成 C 数组，避免手抄造成与 CSV 不一致。"""
    h_path = os.path.join(OUT_DIR, "vocab_data.h")
    c_path = os.path.join(OUT_DIR, "vocab_data.c")

    with open(h_path, "w", encoding="utf-8") as f:
        f.write("/* 自动生成：tools/make_font.py —— 请勿手改 */\n")
        f.write("#pragma once\n\n")
        f.write("typedef struct {\n")
        f.write("    const char *word;\n")
        f.write("    const char *phonetic;\n")
        f.write("    const char *meaning;\n")
        f.write("    const char *example;\n")
        f.write("} vocab_entry_t;\n\n")
        f.write("extern const vocab_entry_t g_vocab[];\n")
        f.write("extern const int g_vocab_count;\n")

    def esc(s):
        return s.replace("\\", "\\\\").replace('"', '\\"')

    with open(c_path, "w", encoding="utf-8") as f:
        f.write("/* 自动生成：tools/make_font.py —— 请勿手改 */\n")
        f.write('#include "vocab_data.h"\n\n')
        f.write("const vocab_entry_t g_vocab[] = {\n")
        for r in rows:
            f.write('    {"%s", "%s", "%s", "%s"},\n' % (
                esc(r["word"]), esc(r["phonetic"]), esc(r["meaning"]), esc(r["example"])))
        f.write("};\n\n")
        f.write("const int g_vocab_count = sizeof(g_vocab) / sizeof(g_vocab[0]);\n")

    print("  词库 %d 条 -> vocab_data.c/h" % len(rows))


def main():
    rows = load_rows()
    latin, cjk = collect_chars(rows)
    os.makedirs(OUT_DIR, exist_ok=True)

    print("拉丁/音标字符 %d 个，汉字 %d 个" % (len(latin), len(cjk)))

    with open(os.path.join(OUT_DIR, "fonts.h"), "w", encoding="utf-8") as f:
        f.write(HEADER)

    jobs = [
        ("font_word.c", "font_word", FONT_WORD, 32, latin),
        ("font_ipa.c", "font_ipa", FONT_LATIN, 16, latin),
        ("font_small.c", "font_small", FONT_LATIN, 12, latin),
        ("font_cjk.c", "font_cjk", FONT_CJK, 16, cjk),
    ]

    total = 0
    for fname, ident, path, size, chars in jobs:
        ascent, descent, glyphs, blob = render(path, size, chars)
        emit_c(os.path.join(OUT_DIR, fname), ident, os.path.basename(path), size,
               ascent, descent, glyphs, blob)
        total += len(blob)
        print("  %-14s %3d 字形 %6d 字节  ascent=%d" % (fname, len(glyphs), len(blob), ascent))

    emit_vocab(rows)
    print("位图合计 %d 字节（%.1f KB）" % (total, total / 1024))


if __name__ == "__main__":
    main()
