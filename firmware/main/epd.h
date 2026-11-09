#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* 面板参数：SSD1680 128x296 */
#define EPD_WIDTH   128
#define EPD_HEIGHT  296
#define EPD_BUF_LEN ((EPD_WIDTH / 8) * EPD_HEIGHT)   /* 4736 字节 */

/* 像素取值（写进 RAM 的位）：1 = 白，0 = 黑 */
#define EPD_WHITE   1
#define EPD_BLACK   0

esp_err_t epd_init(void);

void epd_clear(uint8_t color);
void epd_set_pixel(int x, int y, uint8_t color);
void epd_fill_rect(int x, int y, int w, int h, uint8_t color);
void epd_hline(int x0, int x1, int y, uint8_t color);
void epd_vline(int x, int y0, int y1, uint8_t color);

/* 全刷：同时写入新旧两块 RAM，波形干净但慢（约 2 s） */
uint32_t epd_display_full(void);

/* 局部刷新：只更新 (x,y,w,h) 区域，快但会留残影 */
uint32_t epd_display_partial(int x, int y, int w, int h);

/* 自动模式：比较新旧帧，只刷变化区域；残影累积到阈值时自动转全刷。
 * used_full 返回本次是否为全刷。 */
uint32_t epd_display_auto(bool *used_full);

/* 进入深度睡眠，电流降到 µA 级 */
void epd_sleep(void);
