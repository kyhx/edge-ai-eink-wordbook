/*
 * SSD1680 128x296 墨水屏驱动（4 线 SPI，全刷）
 *
 * 时序要点：
 *   - BUSY 为高表示忙，任何命令（除读忙）都要等它拉低
 *   - 写完 RAM 后需要 0x22(更新序列) + 0x20(激活) 才会真正刷新
 *   - 帧缓存 1 = 白，0 = 黑（SSD1680 的 BW RAM 约定）
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
#define CMD_WRITE_RAM       0x24
#define CMD_BORDER          0x3C
#define CMD_SET_RAM_X       0x44
#define CMD_SET_RAM_Y       0x45
#define CMD_SET_RAM_X_CNT   0x4E
#define CMD_SET_RAM_Y_CNT   0x4F

#define BUSY_TIMEOUT_MS     10000

static spi_device_handle_t s_spi;
static uint8_t s_fb[EPD_BUF_LEN];

/* ---------------------------------------------------------------- */
/* 底层：命令 / 数据                                                 */
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
    gpio_set_level(PIN_EPD_DC, 0);   /* DC = 0：命令 */
    cs_low();
    spi_send(&cmd, 1);
    cs_high();
}

static void epd_data(const uint8_t *data, size_t len)
{
    gpio_set_level(PIN_EPD_DC, 1);   /* DC = 1：数据 */
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
    int waited = 0;
    while (gpio_get_level(PIN_EPD_BUSY) == 1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
        if (waited >= BUSY_TIMEOUT_MS) {
            ESP_LOGE(TAG, "等待 BUSY 超时（%d ms）", waited);
            break;
        }
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

/* ---------------------------------------------------------------- */
/* 初始化                                                            */
/* ---------------------------------------------------------------- */
esp_err_t epd_init(void)
{
    /* GPIO（SCK/SDA 由 SPI 外设接管，不要在这里重复配置，否则会告警） */
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

    /* SPI2 */
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
        .clock_speed_hz = 10 * 1000 * 1000,   /* 10 MHz，保守值 */
        .mode = 0,
        .spics_io_num = -1,                   /* CS 手动控制 */
        .queue_size = 1,
    };
    err = spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI 设备添加失败: %s", esp_err_to_name(err));
        return err;
    }

    /* 上电序列 */
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

    epd_cmd(CMD_SET_RAM_X);               /* 0 ~ 15 (128/8) */
    epd_data1(0x00);
    epd_data1(0x0F);

    epd_cmd(CMD_SET_RAM_Y);               /* 0 ~ 295 */
    epd_data1(0x00);
    epd_data1(0x00);
    epd_data1(0x27);
    epd_data1(0x01);

    epd_cmd(CMD_BORDER);                  /* 边框波形 */
    epd_data1(0x05);

    epd_cmd(CMD_UPDATE_CTRL1);
    epd_data1(0x00);
    epd_data1(0x80);

    epd_cmd(CMD_TEMP_SENSOR);
    epd_data1(0x80);

    vTaskDelay(pdMS_TO_TICKS(10));
    epd_wait_busy();

    memset(s_fb, 0xFF, sizeof(s_fb));     /* 默认全白 */

    ESP_LOGI(TAG, "SSD1680 初始化完成 %dx%d，SPI 10MHz", EPD_WIDTH, EPD_HEIGHT);
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

void epd_set_pixel(int x, int y, uint8_t color)
{
    fb_set(x, y, color);
}

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
uint32_t epd_display(void)
{
    int64_t t0 = esp_timer_get_time();

    epd_wait_busy();

    epd_cmd(CMD_SET_RAM_X_CNT);
    epd_data1(0x00);
    epd_cmd(CMD_SET_RAM_Y_CNT);
    epd_data1(0x00);
    epd_data1(0x00);

    epd_cmd(CMD_WRITE_RAM);
    epd_data(s_fb, sizeof(s_fb));

    epd_cmd(CMD_UPDATE_CTRL2);
    epd_data1(0xF7);                      /* 全刷序列 */
    epd_cmd(CMD_MASTER_ACTIVATE);

    epd_wait_busy();

    return (uint32_t)((esp_timer_get_time() - t0) / 1000);
}

void epd_sleep(void)
{
    epd_cmd(CMD_DEEP_SLEEP);
    epd_data1(0x01);
}
