/*
 * 主程序：墨水屏点亮测试
 *
 * 开机后先画测试图并全刷，之后每 5 秒在正/反两张图之间切换，
 * 同时打印全刷耗时；按 BACK 键触发一次音频回环测试。
 */
#include <inttypes.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"

#include "audio_test.h"
#include "board_config.h"
#include "epd.h"

static const char *TAG = "main";

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

/* 测试图：边框 + 实心块 + 棋盘 + 渐变竖条 + 对角线 */
static void draw_test_card(bool inverse)
{
    uint8_t fg = inverse ? EPD_WHITE : EPD_BLACK;
    uint8_t bg = inverse ? EPD_BLACK : EPD_WHITE;

    epd_clear(bg);

    /* 外边框 */
    epd_hline(0, EPD_WIDTH - 1, 0, fg);
    epd_hline(0, EPD_WIDTH - 1, 1, fg);
    epd_hline(0, EPD_WIDTH - 1, EPD_HEIGHT - 1, fg);
    epd_hline(0, EPD_WIDTH - 1, EPD_HEIGHT - 2, fg);
    epd_vline(0, 0, EPD_HEIGHT - 1, fg);
    epd_vline(1, 0, EPD_HEIGHT - 1, fg);
    epd_vline(EPD_WIDTH - 1, 0, EPD_HEIGHT - 1, fg);
    epd_vline(EPD_WIDTH - 2, 0, EPD_HEIGHT - 1, fg);

    /* 左上角实心块 */
    epd_fill_rect(10, 12, 40, 40, fg);

    /* 右上角 8x8 棋盘，64x64 */
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if ((x + y) & 1) {
                epd_fill_rect(60 + x * 8, 12 + y * 8, 8, 8, fg);
            }
        }
    }

    /* 中部渐变竖条：宽度递增 */
    for (int i = 0; i < 16; i++) {
        epd_fill_rect(10 + i * 7, 90, i / 2 + 1, 60, fg);
    }

    /* 底部四条粗细不同的横线 */
    epd_fill_rect(10, 170, 108, 2,  fg);
    epd_fill_rect(10, 185, 108, 4,  fg);
    epd_fill_rect(10, 200, 108, 8,  fg);
    epd_fill_rect(10, 218, 108, 16, fg);

    /* 对角线 */
    for (int i = 0; i < EPD_WIDTH && (i * 2) < EPD_HEIGHT; i++) {
        epd_set_pixel(i, i * 2, fg);
        epd_set_pixel(EPD_WIDTH - 1 - i, i * 2, fg);
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

    bool inverse = false;
    draw_test_card(inverse);
    uint32_t ms = epd_display();
    ESP_LOGI(TAG, "首帧全刷完成，耗时 %" PRIu32 " ms", ms);

    int64_t last_switch = xTaskGetTickCount();
    bool last_back = true;

    while (true) {
        bool back = gpio_get_level(PIN_KEY_BACK) == 0;
        if (back && !last_back) {
            ESP_LOGI(TAG, "触发音频回环测试");
            audio_test_run_once();
        }
        last_back = back;

        if ((xTaskGetTickCount() - last_switch) >= pdMS_TO_TICKS(5000)) {
            last_switch = xTaskGetTickCount();
            inverse = !inverse;
            draw_test_card(inverse);
            uint32_t cost = epd_display();
            ESP_LOGI(TAG, "全刷 %s图，耗时 %" PRIu32 " ms", inverse ? "反" : "正", cost);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
