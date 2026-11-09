/*
 * SSD1680 128x296 墨水屏驱动（4 线 SPI）
 *
 * 支持全刷与局部刷新：
 *   - 全刷：0x24 + 0x26 写同一帧，0x22=0xF7，约 2 s，无残影
 *   - 局部：只写指定窗口，0x22=0xFF，约 0.3 s，会累积残影
 *
 * 局部刷新依赖两块 RAM 的差分：0x24 放新帧，0x26 放上一帧。
 * 因此驱动内部维护 s_fb（当前）与 s_old（已显示）两份缓存。
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_config.h"
#include "epd.h"

static const char *TAG = "epd";

/* SSD1680 命令 */
#define CMD_DRIVER_OUTPUT   0x01
#define CMD_DEEP_SLEEP      0x10
#define CMD_DATA_ENTRY      0x11
#define CMD_SW_RESET        0x12
#define CMD_TEMP_SENSOR     0x18
#define CMD_MASTER_ACTIVATE 0x20
#define CMD_UPDATE_CTRL1    0x21
#define CMD_UPDATE_CTRL2    0x22
#define CMD_WRITE_RAM_BW    0x24
#define CMD_WRITE_RAM_RED   0x26
#define CMD_BORDER          0x3C
#define CMD_SET_RAM_X       0x44
#define CMD_SET_RAM_Y       0x45
#define CMD_SET_RAM_X_CNT   0x4E
#define CMD_SET_RAM_Y_CNT   0x4F

#define BUSY_TIMEOUT_MS     10000

static spi_device_handle_t s_spi;
static uint8_t s_fb[EPD_BUF_LEN];    /* 当前帧 */
static uint8_t s_old[EPD_BUF_LEN];   /* 屏上已显示的帧 */

/* ---------------------------------------------------------------- */
/* 底层                                                              */
/* ---------------------------------------------------------------- */
static void cs_low(void)  { gpio_set_level(PIN_EPD_CS, 0); }
static void cs_high(void) { gpio_set_level(PIN_EPD_CS, 1); }

static void spi_send(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void epd_cmd(uint8_t cmd)
{
    gpio_set_level(PIN_EPD_DC, 0);
    cs_low();
    spi_send(&cmd, 1);
    cs_high();
}

static void epd_data(const uint8_t *data, size_t len)
{
    gpio_set_level(PIN_EPD_DC, 1);
    cs_low();
    spi_send(data, len);
    cs_high();
}

static void epd_data1(uint8_t v)
{
    epd_data(&v, 1);
}

static void epd_wait_busy(void)
{
    /* 注意：tick 默认 10 ms，pdMS_TO_TICKS(5) 会取整为 0，
     * 用 0 tick 的 delay 会让循环空转、瞬间误判超时。这里按 1 tick 让出 CPU，
     * 超时判断改用 esp_timer 的墙钟时间。 */
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(PIN_EPD_BUSY) == 1) {
        if ((esp_timer_get_time() - start) / 1000 >= BUSY_TIMEOUT_MS) {
            ESP_LOGE(TAG, "等待 BUSY 超时（%d ms）", BUSY_TIMEOUT_MS);
            break;
        }
        vTaskDelay(1);
    }
}

static void epd_reset(void)
{
    gpio_set_level(PIN_EPD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_EPD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_EPD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
}

/* 窗口与光标都以"字节列"为单位 */
static void epd_set_window(int x0, int y0, int x1, int y1)
{
    epd_cmd(CMD_SET_RAM_X);
    epd_data1((uint8_t)(x0 / 8));
    epd_data1((uint8_t)(x1 / 8));

    epd_cmd(CMD_SET_RAM_Y);
    epd_data1((uint8_t)(y0 & 0xFF));
    epd_data1((uint8_t)(y0 >> 8));
    epd_data1((uint8_t)(y1 & 0xFF));
    epd_data1((uint8_t)(y1 >> 8));
}

static void epd_set_cursor(int x0, int y0)
{
    epd_cmd(CMD_SET_RAM_X_CNT);
    epd_data1((uint8_t)(x0 / 8));

    epd_cmd(CMD_SET_RAM_Y_CNT);
    epd_data1((uint8_t)(y0 & 0xFF));
    epd_data1((uint8_t)(y0 >> 8));
}

/* ---------------------------------------------------------------- */
/* 初始化                                                            */
/* ---------------------------------------------------------------- */
esp_err_t epd_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_EPD_CS)  | (1ULL << PIN_EPD_DC) |
                        (1ULL << PIN_EPD_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_EPD_BUSY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    gpio_set_level(PIN_EPD_CS, 1);
    gpio_set_level(PIN_EPD_DC, 0);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_EPD_SDA,
        .miso_io_num = -1,
        .sclk_io_num = PIN_EPD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_BUF_LEN,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI 总线初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    err = spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI 设备添加失败: %s", esp_err_to_name(err));
        return err;
    }

    epd_reset();

    epd_cmd(CMD_SW_RESET);
    vTaskDelay(pdMS_TO_TICKS(10));
    epd_wait_busy();

    epd_cmd(CMD_DRIVER_OUTPUT);           /* MUX = 296 */
    epd_data1(0x27);
    epd_data1(0x01);
    epd_data1(0x00);

    epd_cmd(CMD_DATA_ENTRY);              /* X 增、Y 增 */
    epd_data1(0x03);

    epd_set_window(0, 0, EPD_WIDTH - 1, EPD_HEIGHT - 1);

    epd_cmd(CMD_BORDER);
    epd_data1(0x05);

    epd_cmd(CMD_UPDATE_CTRL1);
    epd_data1(0x00);
    epd_data1(0x80);

    epd_cmd(CMD_TEMP_SENSOR);
    epd_data1(0x80);

    vTaskDelay(pdMS_TO_TICKS(10));
    epd_wait_busy();

    memset(s_fb, 0xFF, sizeof(s_fb));
    memset(s_old, 0xFF, sizeof(s_old));

    ESP_LOGI(TAG, "SSD1680 初始化完成 %dx%d", EPD_WIDTH, EPD_HEIGHT);
    return ESP_OK;
}

/* ---------------------------------------------------------------- */
/* 绘图                                                              */
/* ---------------------------------------------------------------- */
static inline void fb_set(int x, int y, uint8_t color)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;
    int index = (y * (EPD_WIDTH / 8)) + (x >> 3);
    uint8_t bit = 0x80 >> (x & 7);
    if (color) {
        s_fb[index] |= bit;
    } else {
        s_fb[index] &= (uint8_t)~bit;
    }
}

void epd_clear(uint8_t color)
{
    memset(s_fb, color ? 0xFF : 0x00, sizeof(s_fb));
}

void epd_set_pixel(int x, int y, uint8_t color) { fb_set(x, y, color); }

void epd_fill_rect(int x, int y, int w, int h, uint8_t color)
{
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            fb_set(i, j, color);
        }
    }
}

void epd_hline(int x0, int x1, int y, uint8_t color)
{
    for (int x = x0; x <= x1; x++) fb_set(x, y, color);
}

void epd_vline(int x, int y0, int y1, uint8_t color)
{
    for (int y = y0; y <= y1; y++) fb_set(x, y, color);
}

/* ---------------------------------------------------------------- */
/* 刷新                                                              */
/* ---------------------------------------------------------------- */
uint32_t epd_display_full(void)
{
    int64_t t0 = esp_timer_get_time();

    epd_wait_busy();
    epd_set_window(0, 0, EPD_WIDTH - 1, EPD_HEIGHT - 1);
    epd_set_cursor(0, 0);

    epd_cmd(CMD_WRITE_RAM_BW);
    epd_data(s_fb, sizeof(s_fb));

    epd_cmd(CMD_WRITE_RAM_RED);
    epd_data(s_fb, sizeof(s_fb));

    epd_cmd(CMD_UPDATE_CTRL2);
    epd_data1(0xF7);
    epd_cmd(CMD_MASTER_ACTIVATE);
    epd_wait_busy();

    memcpy(s_old, s_fb, sizeof(s_old));
    return (uint32_t)((esp_timer_get_time() - t0) / 1000);
}

uint32_t epd_display_partial(int x, int y, int w, int h)
{
    int64_t t0 = esp_timer_get_time();

    int x0 = x & ~7;                       /* 按字节对齐 */
    int x1 = (x + w - 1) | 7;
    if (x1 > EPD_WIDTH - 1)  x1 = EPD_WIDTH - 1;
    if (x0 < 0)              x0 = 0;
    int y0 = y;
    int y1 = y + h - 1;
    if (y1 > EPD_HEIGHT - 1) y1 = EPD_HEIGHT - 1;
    if (y0 < 0)              y0 = 0;

    int row_bytes = (x1 - x0 + 1) / 8;
    int col_off   = x0 / 8;

    epd_wait_busy();
    epd_set_window(x0, y0, x1, y1);
    epd_set_cursor(x0, y0);

    /* 新帧 */
    epd_cmd(CMD_WRITE_RAM_BW);
    gpio_set_level(PIN_EPD_DC, 1);
    cs_low();
    for (int yy = y0; yy <= y1; yy++) {
        spi_send(&s_fb[yy * (EPD_WIDTH / 8) + col_off], row_bytes);
    }
    cs_high();

    /* 旧帧：供控制器做差分 */
    epd_cmd(CMD_WRITE_RAM_RED);
    gpio_set_level(PIN_EPD_DC, 1);
    cs_low();
    for (int yy = y0; yy <= y1; yy++) {
        spi_send(&s_old[yy * (EPD_WIDTH / 8) + col_off], row_bytes);
    }
    cs_high();

    epd_cmd(CMD_UPDATE_CTRL2);
    epd_data1(0xFF);
    epd_cmd(CMD_MASTER_ACTIVATE);
    epd_wait_busy();

    memcpy(s_old, s_fb, sizeof(s_old));
    return (uint32_t)((esp_timer_get_time() - t0) / 1000);
}

void epd_sleep(void)
{
    epd_cmd(CMD_DEEP_SLEEP);
    epd_data1(0x01);
}

/* ---------------------------------------------------------------- */
/* 自动刷新：脏区检测 + 定期全刷清除残影                              */
/* ---------------------------------------------------------------- */
#define PARTIAL_LIMIT   5       /* 连续局部刷新次数上限：残影明显时降到 5 */
#define FULL_AREA_RATIO 70      /* 变化面积超过 70% 就直接全刷 */
#define DIRTY_PAD       3       /* 脏区外扩像素，避免边缘残影 */

uint32_t epd_display_auto(bool *used_full)
{
    static int partial_count = 0;
    static bool base_ready = false;
    if (used_full) *used_full = false;

    /* 第一次必须全刷：局部刷新依赖控制器里的基准图 */
    if (!base_ready) {
        base_ready = true;
        if (used_full) *used_full = true;
        return epd_display_full();
    }

    int min_b = EPD_WIDTH / 8, max_b = -1;
    int min_y = EPD_HEIGHT,   max_y = -1;

    for (int y = 0; y < EPD_HEIGHT; y++) {
        const uint8_t *cur = &s_fb[y * (EPD_WIDTH / 8)];
        const uint8_t *old = &s_old[y * (EPD_WIDTH / 8)];
        for (int b = 0; b < EPD_WIDTH / 8; b++) {
            if (cur[b] != old[b]) {
                if (b < min_b) min_b = b;
                if (b > max_b) max_b = b;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
        }
    }

    if (max_b < 0) return 0;                 /* 无变化 */

    int x0 = min_b * 8 - DIRTY_PAD;
    int x1 = max_b * 8 + 7 + DIRTY_PAD;
    int y0 = min_y - DIRTY_PAD;
    int y1 = max_y + DIRTY_PAD;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > EPD_WIDTH - 1)  x1 = EPD_WIDTH - 1;
    if (y1 > EPD_HEIGHT - 1) y1 = EPD_HEIGHT - 1;

    int w  = x1 - x0 + 1;
    int h  = y1 - y0 + 1;

    int area_ratio = (w * h) * 100 / (EPD_WIDTH * EPD_HEIGHT);
    if (partial_count >= PARTIAL_LIMIT || area_ratio > FULL_AREA_RATIO) {
        partial_count = 0;
        if (used_full) *used_full = true;
        return epd_display_full();
    }

    partial_count++;
    return epd_display_partial(x0, y0, w, h);
}
