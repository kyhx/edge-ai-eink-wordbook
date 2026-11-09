/*
 * 主程序：单词卡界面
 *
 * 横屏 296x128，显示单词 / 音标 / 释义 / 例句。
 * 按 NEXT 下一个词，PREV 上一个词，BACK 触发音频回环测试（后续改为播放单词发音）。
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_psram.h"

#include "audio_test.h"
#include "board_config.h"
#include "epd.h"
#include "gfx.h"
#include "vocab_data.h"

static const char *TAG = "main";

static int s_index = 0;

static void draw_header(int idx, int total)
{
    gfx_fill(0, 0, GFX_W, 16, EPD_BLACK);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d / %d", idx + 1, total);
    gfx_text(&font_small, 6, 12, buf, EPD_WHITE);

    /* 右对齐，避免文字超出画布被裁掉 */
    const char *brand = "EINK TUTOR";
    int w = gfx_text_width(&font_small, brand);
    gfx_text(&font_small, GFX_W - w - 6, 12, brand, EPD_WHITE);
}

static void draw_card(int idx)
{
    const vocab_entry_t *v = &g_vocab[idx];

    gfx_clear(EPD_WHITE);
    draw_header(idx, g_vocab_count);

    /* 长单词自动降到 24px，避免超出右边界 */
    const bitmap_font_t *wf = &font_word;
    if (gfx_text_width(&font_word, v->word) > GFX_W - 20) {
        wf = &font_word_s;
    }
    gfx_text(wf, 10, 56, v->word, EPD_BLACK);
    gfx_text(&font_ipa, 12, 78, v->phonetic, EPD_BLACK);

    gfx_hline(10, GFX_W - 10, 88, EPD_BLACK);

    gfx_text(&font_cjk, 12, 108, v->meaning, EPD_BLACK);
    gfx_text(&font_small, 12, 124, v->example, EPD_BLACK);
}

static void show(int idx)
{
    draw_card(idx);
    bool full = false;
    uint32_t ms = epd_display_auto(&full);
    ESP_LOGI(TAG, "[%d/%d] %s %s %s | %s %" PRIu32 " ms",
             idx + 1, g_vocab_count, g_vocab[idx].word,
             g_vocab[idx].phonetic, g_vocab[idx].meaning,
             full ? "全刷" : "局部", ms);
}

static void init_gpio(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_AMP_EN) | (1ULL << PIN_MIC_PWR_EN),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(PIN_AMP_EN, 0);
    gpio_set_level(PIN_MIC_PWR_EN, 1);

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_KEY_PREV) | (1ULL << PIN_KEY_NEXT) |
                        (1ULL << PIN_KEY_OK)   | (1ULL << PIN_KEY_BACK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));
}

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "ESP32-S3 %d 核 | PSRAM %u KB | 词库 %d 条",
             chip.cores, (unsigned)(esp_psram_get_size() / 1024), g_vocab_count);

    init_gpio();
    if (epd_init() != ESP_OK) {
        ESP_LOGE(TAG, "墨水屏初始化失败");
        return;
    }
    audio_test_init();

    show(s_index);

    bool last_prev = true, last_next = true, last_back = true;

    while (true) {
        bool prev = gpio_get_level(PIN_KEY_PREV) == 0;
        bool next = gpio_get_level(PIN_KEY_NEXT) == 0;
        bool back = gpio_get_level(PIN_KEY_BACK) == 0;

        if (next && !last_next) {
            s_index = (s_index + 1) % g_vocab_count;
            show(s_index);
        }
        if (prev && !last_prev) {
            s_index = (s_index + g_vocab_count - 1) % g_vocab_count;
            show(s_index);
        }
        if (back && !last_back) {
            ESP_LOGI(TAG, "音频回环测试");
            audio_test_run_once();
            show(s_index);          /* 测试后重画，避免被日志遮挡 */
        }

        last_prev = prev;
        last_next = next;
        last_back = back;

        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
