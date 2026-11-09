/* 自动生成：tools/make_font.py —— 请勿手改 */
#pragma once

typedef struct {
    const char *word;
    const char *phonetic;
    const char *meaning;
    const char *example;
} vocab_entry_t;

extern const vocab_entry_t g_vocab[];
extern const int g_vocab_count;
