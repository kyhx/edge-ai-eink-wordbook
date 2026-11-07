//
// board_config.h —— 板级引脚定义
//
// 唯一来源：hardware/wiring/pinmap.md
// 严禁在组件里直接写 GPIO 数字，一律引用这里的宏。
//
#pragma once

/* ---------------- 墨水屏 SSD1680 128x296（SPI2 / FSPI） ---------------- */
#define PIN_EPD_SCK     12
#define PIN_EPD_MOSI    11
#define PIN_EPD_CS      10
#define PIN_EPD_DC       9
#define PIN_EPD_RST      8
#define PIN_EPD_BUSY     7

/* ---------------- 音频输入 ICS-43434（I2S0） ---------------- */
#define PIN_MIC_BCLK    15
#define PIN_MIC_WS      16
#define PIN_MIC_DIN     17

/* ---------------- 音频输出 MAX98357A（I2S1） ---------------- */
#define PIN_SPK_BCLK    18
#define PIN_SPK_WS      21
#define PIN_SPK_DOUT    39

/* ---------------- 按键（低有效，内部上拉） ---------------- */
#define PIN_KEY_PREV     2
#define PIN_KEY_NEXT     4
#define PIN_KEY_OK       5
#define PIN_KEY_BACK    40

/* ---------------- 模拟与电源 ---------------- */
#define PIN_BAT_ADC      1   /* ADC1_CH0 */
#define PIN_AMP_EN      41   /* MAX98357 SD：高=工作，低=关断 */
#define PIN_MIC_PWR_EN  42   /* 可选：麦克风负载开关 */

/* ---------------- 存储 ---------------- */
/* 不使用 SD 卡：词库音频编进 Flash 分区，学习记录走 NVS */
