/*
 * 启动自检 + I2S 回环测试
 *
 * 流程：开机打印芯片信息 → 自动录 3 秒 → 从功放回放；之后按 OK 键可重复
 *
 * 采样参数：
 *   麦克风 ICS-43434  I2S0 主机接收，32 bit 槽 / 单声道（左）
 *   功放   MAX98357   I2S1 主机发送，16 bit / 立体声
 *   采样率 16 kHz
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "esp_system.h"

#include "board_config.h"

static const char *TAG = "selftest";

#define SAMPLE_RATE      16000
#define REC_SECONDS      3
#define REC_SAMPLES      (SAMPLE_RATE * REC_SECONDS)
#define REC_BYTES        (REC_SAMPLES * 4)        /* 32 bit 槽，单声道 */
#define PLAY_BYTES       (REC_SAMPLES * 2 * 2)    /* 16 bit，立体声 */

static i2s_chan_handle_t s_rx = NULL;
static i2s_chan_handle_t s_tx = NULL;

/* ---------------------------------------------------------------- */
/* 芯片信息                                                          */
/* ---------------------------------------------------------------- */
static const char *chip_model_name(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32S3: return "ESP32-S3";
    default:           return "unknown";
    }
}

static void print_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    ESP_LOGI(TAG, "================ 启动自检 ================");
    ESP_LOGI(TAG, "芯片      : %s, %d 核, 版本 v%d.%d",
             chip_model_name(chip.model), chip.cores,
             chip.revision / 100, chip.revision % 100);

    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Flash     : %" PRIu32 " MB", flash_size / (1024 * 1024));
    }

    size_t psram = esp_psram_get_size();
    ESP_LOGI(TAG, "PSRAM     : %u KB", (unsigned)(psram / 1024));
    ESP_LOGI(TAG, "内部RAM   : 空闲 %u KB / 总 %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024));
    ESP_LOGI(TAG, "==========================================");
}

/* ---------------------------------------------------------------- */
/* GPIO                                                              */
/* ---------------------------------------------------------------- */
static void init_gpio(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_AMP_EN) | (1ULL << PIN_MIC_PWR_EN),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(PIN_AMP_EN, 0);      /* 功放先关断，避免上电爆音 */
    gpio_set_level(PIN_MIC_PWR_EN, 1);  /* 麦克风上电 */

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_KEY_PREV) | (1ULL << PIN_KEY_NEXT) |
                        (1ULL << PIN_KEY_OK)   | (1ULL << PIN_KEY_BACK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));
}

/* ---------------------------------------------------------------- */
/* I2S                                                               */
/* ---------------------------------------------------------------- */
static void audio_init(void)
{
    /* --- 麦克风：I2S0 接收 --- */
    i2s_chan_config_t rx_cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_cc, NULL, &s_rx));

    i2s_std_config_t rx_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_MIC_BCLK,
            .ws   = PIN_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = PIN_MIC_DIN,
            .invert_flags = { .mclk_inv = 0, .bclk_inv = 0, .ws_inv = 0 },
        },
    };
    rx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;   /* ICS-43434 的 LR 接地选左声道 */
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &rx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx));

    /* --- 功放：I2S1 发送 --- */
    i2s_chan_config_t tx_cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_cc, &s_tx, NULL));

    i2s_std_config_t tx_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_SPK_BCLK,
            .ws   = PIN_SPK_WS,
            .dout = PIN_SPK_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = 0, .bclk_inv = 0, .ws_inv = 0 },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &tx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));

    ESP_LOGI(TAG, "I2S 初始化完成：MIC bclk=%d ws=%d din=%d | AMP bclk=%d ws=%d dout=%d",
             PIN_MIC_BCLK, PIN_MIC_WS, PIN_MIC_DIN,
             PIN_SPK_BCLK, PIN_SPK_WS, PIN_SPK_DOUT);
}

/* ---------------------------------------------------------------- */
/* 回环测试                                                          */
/* ---------------------------------------------------------------- */
static void run_loopback(uint8_t *rec, int16_t *play)
{
    ESP_LOGI(TAG, "录音 %d 秒 ……", REC_SECONDS);

    size_t got = 0;
    while (got < REC_BYTES) {
        size_t n = 0;
        esp_err_t err = i2s_channel_read(s_rx, rec + got, REC_BYTES - got, &n, 1000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "读取失败: %s", esp_err_to_name(err));
            return;
        }
        got += n;
    }

    /* 32 bit 槽里高 24 位是有效数据，取高 16 位后做峰值归一化 */
    int32_t *in = (int32_t *)rec;
    int32_t peak = 0;
    int64_t sum_abs = 0;
    for (int i = 0; i < REC_SAMPLES; i++) {
        int32_t v = in[i] >> 16;
        int32_t a = v < 0 ? -v : v;
        if (a > peak) peak = a;
        sum_abs += a;
    }
    size_t mean_abs = (size_t)(sum_abs / REC_SAMPLES);

    ESP_LOGI(TAG, "录音完成：峰值 %" PRId32 " (%.1f%% FS)，平均 |x| = %u",
             peak, peak * 100.0f / 32768.0f, (unsigned)mean_abs);

    if (peak < 100) {
        ESP_LOGW(TAG, "信号几乎为零，请检查麦克风接线（SCK/WS/SD 与供电）");
    }

    int32_t gain_q8 = peak > 0 ? (int32_t)((30000LL << 8) / peak) : 256;
    if (gain_q8 > 32 * 256) gain_q8 = 32 * 256;   /* 最多放大 32 倍 */

    for (int i = 0; i < REC_SAMPLES; i++) {
        int32_t v = (in[i] >> 16) * gain_q8 / 256;
        if (v > 32767)  v = 32767;
        if (v < -32768) v = -32768;
        play[2 * i]     = (int16_t)v;
        play[2 * i + 1] = (int16_t)v;
    }

    gpio_set_level(PIN_AMP_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "回放中 ……");
    size_t written = 0;
    while (written < PLAY_BYTES) {
        size_t w = 0;
        esp_err_t err = i2s_channel_write(s_tx, (uint8_t *)play + written,
                                          PLAY_BYTES - written, &w, 1000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "写入失败: %s", esp_err_to_name(err));
            break;
        }
        written += w;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(PIN_AMP_EN, 0);
    ESP_LOGI(TAG, "回放结束，按 OK 键可再测一次");
}

/* ---------------------------------------------------------------- */
/* 主程序                                                            */
/* ---------------------------------------------------------------- */
void app_main(void)
{
    print_info();
    init_gpio();

    uint8_t *rec  = heap_caps_malloc(REC_BYTES,  MALLOC_CAP_SPIRAM);
    int16_t *play = heap_caps_malloc(PLAY_BYTES, MALLOC_CAP_SPIRAM);
    if (!rec || !play) {
        ESP_LOGE(TAG, "内存分配失败：rec=%p play=%p", rec, play);
        return;
    }

    audio_init();

    ESP_LOGI(TAG, "上电自动测试一次");
    run_loopback(rec, play);

    bool last_ok = true;
    while (true) {
        bool ok = gpio_get_level(PIN_KEY_OK) == 0;   /* 按下为低 */
        if (ok && !last_ok) {
            run_loopback(rec, play);
        }
        last_ok = ok;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
