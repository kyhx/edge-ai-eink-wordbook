/*
 * 主程序：局部刷新演示
 *
 * 开机全刷一次画出固定版面，之后每秒只局部刷新底部的进度条，
 * 观察残影累积情况；每 15 次局部刷新插入一次全刷清除残影。
 * 按 BACK 键触发一次音频回环测试。
 */
#include <inttypes.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"

#include "audio_test.h"
#include "board_config.h"
#include "epd.h"

static const char *TAG = "main";

/* 进度条区域（局部刷新的窗口） */
#define BAR_X      12
#define BAR_Y      200
#define BAR_W      104
#define BAR_H      20
#define BAR_PAD    4
#define FULL_EVERY 15

static void print_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "ESP32-S3 %d 核 v%d.%d | Flash %" PRIu32 " MB | PSRAM %u KB",
             chip.cores, chip.revision / 100, chip.revision % 100,
             flash_size / (1024 * 1024), (unsigned)(esp_psram_get_size() / 1024));
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

/* 固定版面：只在全刷时画 */
static void draw_static_layout(void)
{
    epd_clear(EPD_WHITE);

    /* 外边框 */
    epd_hline(0, EPD_WIDTH - 1, 0, EPD_BLACK);
    epd_hline(0, EPD_WIDTH - 1, EPD_HEIGHT - 1, EPD_BLACK);
    epd_vline(0, 0, EPD_HEIGHT - 1, EPD_BLACK);
    epd_vline(EPD_WIDTH - 1, 0, EPD_HEIGHT - 1, EPD_BLACK);

    /* 顶部标题条（黑底，里面挖几条白线模拟文字） */
    epd_fill_rect(4, 4, EPD_WIDTH - 8, 40, EPD_BLACK);
    for (int i = 0; i < 4; i++) {
        epd_fill_rect(10, 10 + i * 9, 60 + i * 12, 4, EPD_WHITE);
    }
    epd_fill_rect(100, 10, 20, 28, EPD_WHITE);

    /* 正文区域的"卡片"轮廓 */
    epd_fill_rect(10, 60, EPD_WIDTH - 20, 120, EPD_BLACK);
    epd_fill_rect(14, 64, EPD_WIDTH - 28, 112, EPD_WHITE);

    /* 卡片内的文字条 */
    for (int i = 0; i < 6; i++) {
        int w = 20 + (i * 17) % 80;
        epd_fill_rect(22, 74 + i * 16, w, 6, EPD_BLACK);
    }

    /* 进度条外框 */
    epd_fill_rect(BAR_X, BAR_Y, BAR_W, BAR_H, EPD_BLACK);
    epd_fill_rect(BAR_X + 2, BAR_Y + 2, BAR_W - 4, BAR_H - 4, EPD_WHITE);

    /* 底部说明块 */
    epd_fill_rect(10, 240, 50, 8, EPD_BLACK);
    epd_fill_rect(10, 256, 90, 8, EPD_BLACK);
    epd_fill_rect(10, 272, 70, 8, EPD_BLACK);
}

/* 进度条内容，0~100 */
static void draw_progress(int percent)
{
    int inner_w = BAR_W - 2 * BAR_PAD;

    /* 先清空条内区域 */
    epd_fill_rect(BAR_X + 2, BAR_Y + 2, BAR_W - 4, BAR_H - 4, EPD_WHITE);

    int w = inner_w * percent / 100;
    if (w > 0) {
        epd_fill_rect(BAR_X + BAR_PAD, BAR_Y + BAR_PAD, w, BAR_H - 2 * BAR_PAD, EPD_BLACK);
    }
}

void app_main(void)
{
    print_info();
    init_gpio();

    if (epd_init() != ESP_OK) {
        ESP_LOGE(TAG, "墨水屏初始化失败");
        return;
    }

    audio_test_init();

    draw_static_layout();
    draw_progress(0);
    uint32_t ms = epd_display_full();
    ESP_LOGI(TAG, "版面全刷完成，耗时 %" PRIu32 " ms", ms);

    int percent = 0;
    int since_full = 0;
    bool last_back = true;

    while (true) {
        bool back = gpio_get_level(PIN_KEY_BACK) == 0;
        if (back && !last_back) {
            ESP_LOGI(TAG, "触发音频回环测试");
            audio_test_run_once();
        }
        last_back = back;

        percent += 5;
        if (percent > 100) percent = 0;

        if (since_full >= FULL_EVERY) {
            /* 定期全刷清除残影 */
            draw_static_layout();
            draw_progress(percent);
            uint32_t cost = epd_display_full();
            ESP_LOGI(TAG, "残影清理：全刷 %" PRIu32 " ms", cost);
            since_full = 0;
        } else {
            draw_progress(percent);
            uint32_t cost = epd_display_partial(BAR_X - 2, BAR_Y - 2, BAR_W + 4, BAR_H + 4);
            ESP_LOGI(TAG, "局部刷新 %d%% 耗时 %" PRIu32 " ms", percent, cost);
            since_full++;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
