/*
 * I2S 音频回环测试
 *   ICS-43434 → I2S0 接收（32 bit 槽 / 单声道左）
 *   MAX98357  → I2S1 发送（16 bit / 立体声）
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "audio_test.h"
#include "board_config.h"

static const char *TAG = "audio";

#define SAMPLE_RATE   16000
#define REC_SECONDS   3
#define REC_SAMPLES   (SAMPLE_RATE * REC_SECONDS)
#define REC_BYTES     (REC_SAMPLES * 4)
#define PLAY_BYTES    (REC_SAMPLES * 2 * 2)

static i2s_chan_handle_t s_rx = NULL;
static i2s_chan_handle_t s_tx = NULL;
static uint8_t *s_rec  = NULL;
static int16_t *s_play = NULL;

void audio_test_init(void)
{
    s_rec  = heap_caps_malloc(REC_BYTES,  MALLOC_CAP_SPIRAM);
    s_play = heap_caps_malloc(PLAY_BYTES, MALLOC_CAP_SPIRAM);
    if (!s_rec || !s_play) {
        ESP_LOGE(TAG, "内存分配失败");
        return;
    }

    /* 麦克风：I2S0 接收 */
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

    /* 功放：I2S1 发送 */
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

    ESP_LOGI(TAG, "I2S 就绪：MIC 15/16/17，AMP 18/21/11");
}

void audio_test_run_once(void)
{
    if (!s_rx || !s_tx) {
        ESP_LOGE(TAG, "音频未初始化");
        return;
    }

    ESP_LOGI(TAG, "录音 %d 秒 ……", REC_SECONDS);

    size_t got = 0;
    while (got < REC_BYTES) {
        size_t n = 0;
        esp_err_t err = i2s_channel_read(s_rx, s_rec + got, REC_BYTES - got, &n, 1000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "读取失败: %s", esp_err_to_name(err));
            return;
        }
        got += n;
    }

    int32_t *in = (int32_t *)s_rec;
    int32_t peak = 0;
    int64_t sum_abs = 0;
    for (int i = 0; i < REC_SAMPLES; i++) {
        int32_t v = in[i] >> 16;
        int32_t a = v < 0 ? -v : v;
        if (a > peak) peak = a;
        sum_abs += a;
    }

    ESP_LOGI(TAG, "录音完成：峰值 %" PRId32 " (%.1f%% FS)，平均 |x| = %u",
             peak, peak * 100.0f / 32768.0f, (unsigned)(sum_abs / REC_SAMPLES));

    if (peak < 100) {
        ESP_LOGW(TAG, "信号几乎为零，请检查麦克风接线");
    }

    int32_t gain_q8 = peak > 0 ? (int32_t)((30000LL << 8) / peak) : 256;
    if (gain_q8 > 32 * 256) gain_q8 = 32 * 256;

    for (int i = 0; i < REC_SAMPLES; i++) {
        int32_t v = (in[i] >> 16) * gain_q8 / 256;
        if (v > 32767)  v = 32767;
        if (v < -32768) v = -32768;
        s_play[2 * i]     = (int16_t)v;
        s_play[2 * i + 1] = (int16_t)v;
    }

    gpio_set_level(PIN_AMP_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "回放中 ……");
    size_t written = 0;
    while (written < PLAY_BYTES) {
        size_t w = 0;
        esp_err_t err = i2s_channel_write(s_tx, (uint8_t *)s_play + written,
                                          PLAY_BYTES - written, &w, 1000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "写入失败: %s", esp_err_to_name(err));
            break;
        }
        written += w;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(PIN_AMP_EN, 0);
    ESP_LOGI(TAG, "回放结束");
}
