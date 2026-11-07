# 引脚分配方案（ESP32-S3-N16R8）

本文确定学习机原型在 **YD-ESP32-S3-N16R8 开发板**上的引脚分配，并说明依据与禁忌。后续自制 PCB 沿用同一套编号，便于固件迁移。

依据：
- 板卡手册 `docs/ESP32-S3-N16R8_User_Guide.pdf`（引脚排布、板载外设占用）
- [ESP32-S3 硬件设计指南 · 原理图检查清单](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html)（PSRAM 占用、strapping、上电毛刺、ADC 归属）

## 一、先排除：这些脚不能用

N16R8 是 **16 MB Flash + 8 MB Octal PSRAM**，八线 PSRAM 占用的引脚在芯片内部已连到存储，不能再接外设。

| 引脚 | 状态 | 原因 |
|---|---|---|
| GPIO26–32 | 不引出 | 片内 Flash/PSRAM 的 SPI 总线（SPICLK/CS/D/Q/WP/HD） |
| **GPIO33–37** | **禁止使用** | Octal PSRAM 的 DQ4–DQ7 与 DQS/DM；且工作在 1.8 V 域，接 3.3 V 外设会损坏 |
| GPIO45 | 慎用 | strapping：决定 VDD_SPI 电压（0 = 3.3 V，1 = 1.8 V） |
| GPIO46 | 慎用 | strapping：启动时必须为低，否则可能进不了 SPI Boot |
| GPIO0 | 慎用 | strapping：启动时必须为高；板载 BOOT 键已占用 |
| GPIO3 | 慎用 | strapping：JTAG 信号源选择 |
| GPIO19/20 | 慎用 | USB D-/D+；占用后失去 USB 下载与 USB-JTAG |
| GPIO43/44 | 慎用 | UART0 TX/RX；板载 CH343 与 TX/RX 指示灯占用，占用后没有串口日志 |
| GPIO48 | 视情况 | 板载 WS2812 的 RGB LED；需焊盘短接才接通，短接后只能驱动该灯 |

> 板子排针上虽然有 35/36/37，但**接了也没用**，手册明确写了这三个脚用于芯片与存储之间的内部通信。

## 二、可用引脚池

排除上表后，排针上真正可自由分配的是 26 个：

```
1  2  4  5  6  7  8  9  10  11  12  13  14  15  16  17  18  21
38 39 40 41 42 47  0(可用作按键) 19/20(放弃USB时) 43/44(放弃串口时)
```

其中 **GPIO1–10 属于 ADC1**，是唯一能在开 Wi-Fi 时正常采样的 ADC；GPIO11–20 属 ADC2，Wi-Fi 工作时 ADC2 不可用。**RTC 域只有 GPIO0–21**，只有这些脚能在 Deep-sleep 中唤醒。

## 三、分配方案

### 已确认器件

| 器件 | 型号 | 关键参数 | 接口 |
|---|---|---|---|
| 主控 | ESP32-S3-WROOM-1-N16R8 | 16 MB Flash + 8 MB Octal PSRAM | — |
| 墨水屏 | ZJY128296-029EAAMFGN（SSD1680） | 2.9 寸，128×296，黑白 | 4 线 SPI + DC/RST/BUSY |
| 麦克风 | ICS-43434 | 数字 MEMS，1.65–3.6 V，SNR 65 dB | I2S（无需 MCLK） |
| 功放 | MAX98357A | I2S 直入 D 类，2.5–5.5 V | I2S + SD 使能 |
| 扬声器 | 8 Ω / 1 W | 3.3 V 供电时实际输出约 0.4 W，余量充足 | — |
| 存储 | 片内 16 MB Flash（不装 SD） | 词库音频 + 学习记录 | — |

### 墨水屏（SPI2 / FSPI）

| 信号 | GPIO | 说明 |
|---|---|---|
| EPD_SCK | 12 | FSPICLK 的 IO MUX 默认脚，可跑高频 |
| EPD_MOSI | 11 | FSPID 默认脚 |
| EPD_CS | 10 | FSPICS0 默认脚 |
| EPD_DC | 9 | 数据/命令，任意 GPIO |
| EPD_RST | 8 | 复位，建议外接 RC 延时 |
| EPD_BUSY | 7 | 输入，内部上拉 |

用 9–12 这四个 IO MUX 默认脚是为了让 SPI 走硬件直连（不经 GPIO Matrix），刷新时序更稳，也方便以后跑到 40 MHz 以上。

SSD1680 的几点注意：

- 分辨率 128×296，**横屏使用时为 296×128**，UI 按 296 宽排版。
- BUSY 为高表示忙，全刷典型 2 s、局部 0.3 s 量级；驱动必须等 BUSY 拉低再发下一条命令。
- 支持窗口局部刷新，但 2.9 寸黑白屏局部刷新的残影较明显，策略上按"每 N 次局部刷 + 1 次全刷"处理，N 由实测确定。
- 逻辑电平 3.3 V，刷新峰值电流约 10 mA，休眠约 2 µA。

### 存储方案（不使用 SD）

确定不装 microSD，SPI2 只挂墨水屏，13/14 释放为备用。

理由与分工：

- 词库 50 词、每词约 1 s 音频（16 kHz/16 bit）约 32 KB，**合计约 1.6 MB**，16 MB Flash 余量充足。
- 单词音频编译进 Flash 的独立分区（只读），播放无寻道、无总线争用，翻页与发音不会互相阻塞。
- 学习记录、复习进度等小数据用 NVS 保存，带掉电保护，不需要文件系统。
- 数据导出走 Wi-Fi 同步或 USB 串口，不依赖拔卡。
- 去掉 SD 后省掉卡座、卡检测、FATFS 挂载与掉电损坏兜底，也少一路突发电流。

> 若后续确需扩展容量，优先考虑外挂 SPI Flash，而不是 SD 卡。

### 音频输入（I2S0）

| 信号 | GPIO | 说明 |
|---|---|---|
| MIC_BCLK | 15 | ICS-43434 的 SCK |
| MIC_WS | 16 | ICS-43434 的 WS，接 GND 选左声道 |
| MIC_DIN | 17 | ICS-43434 的 SD（数据输出） |

ICS-43434 不需要 MCLK，ESP-IDF 的 I2S 直接配置成 `I2S_STD_MSB_SLOT` / 24 bit 即可。

### 音频输出（I2S1）

| 信号 | GPIO | 说明 |
|---|---|---|
| SPK_BCLK | 18 | MAX98357 的 BCLK |
| SPK_WS | 21 | MAX98357 的 LRC |
| SPK_DOUT | 39 | MAX98357 的 DIN |

MAX98357 的 GAIN 由模块上的分压电阻决定（默认 9 dB），不改动。8 Ω 负载在 3.3 V 下输出约 0.4 W，1 W 扬声器有充足余量，失真也更低。

### 按键

| 信号 | GPIO | 说明 |
|---|---|---|
| KEY_PREV | 2 | RTC 域，可作 Deep-sleep 唤醒源 |
| KEY_NEXT | 4 | RTC 域 |
| KEY_OK | 5 | RTC 域 |
| KEY_BACK | 40 | 非 RTC；仅用于运行中操作 |

四个键均按"内部上拉 + 按下接地"设计。**没有用 GPIO3**，就是为了避开 strapping。

### 模拟与电源控制

| 信号 | GPIO | 说明 |
|---|---|---|
| BAT_ADC | 1 | ADC1_CH0，电池分压采样，并联 0.1 µF |
| AMP_EN | 41 | 接 MAX98357 的 SD 脚：高=工作，低=关断 |
| MIC_PWR_EN | 42 | 可选，控制麦克风的 P-MOS 负载开关；不做电源门控时该脚空出 |

MAX98357 的 SD 脚兼作声道选择（接 VDD 取左声道、接 GND 取右声道），用 GPIO 驱动时退出关断即为高电平，等于固定选一个声道。若模块没有引出 SD，则 41 改为控制功放供电的负载开关。

### 保留未分配

| 引脚 | 预留用途 |
|---|---|
| 13 | 原 SD_MISO，现空出（FSPIQ 默认脚，留给 SPI2 扩展） |
| 14 | 原 SD_CS，现空出 |
| 38 | 备用（可做第二路按键或传感器中断） |
| 47 | 备用 |
| 0 | BOOT 键，可复用为功能键 |
| 19/20 | USB，留给下载与调试 |
| 43/44 | UART0，留给日志 |
| 48 | RGB LED（需焊盘短接） |

## 四、上电时序上的坑

这三条来自 Espressif 的检查清单，直接决定电路怎么画：

1. **GPIO1–14、17–20 在上电时有约 60 µs 的低电平毛刺。** 若把墨水屏 RST 直接接在这些脚上，可能出现上电瞬间误复位。方案是给 RST 加 RC 延时，或换到无毛刺的脚（38–42、47）。当前把 RST 放在 GPIO8 属于前者，**PCB 上必须留 RC**。
2. **GPIO19/20 上电时有两次约 60 µs 的高电平毛刺，总持续约 3.2 ms。** 所以功放使能 **不能**放在 19/20，否则每次上电扬声器会"啪"一声。
3. **GPIO46 启动时必须为低。** 任何接到 46 的外部电路都不能在上电瞬间把它拉高，包括某些带拉高的模块。同理 GPIO0 不能被拉低。

## 五、几个取舍的理由

- **为什么按键不放 GPIO3/45/46**：省一个脚不值得冒进不了 Boot 的风险，尤其调试阶段会反复复位。
- **为什么电池采样放 GPIO1**：ADC1 在 Wi-Fi 工作时仍可用，ADC2 不行；而设备要联网同步，采样必须随时可靠。
- **为什么麦克风和扬声器分两组 I2S**：ESP32-S3 有 I2S0/I2S1 两个控制器，分开后录音与播放可以并行，代价只是多两个引脚。
- **为什么留 38/47 空着**：调试阶段经常需要临时接逻辑分析仪或补一个按键，预留两脚比事后飞线便宜。
- **为什么砍掉 SD 卡**：SD 与墨水屏共用 SPI2 会互相阻塞，且锂电设备掉电时 FAT32 有损坏风险；1.6 MB 的词库音频放进 16 MB Flash 更简单可靠。

## 六、待确认

| 项 | 状态 | 影响 |
|---|---|---|
| 墨水屏型号 | ✅ ZJY128296-029EAAMFGN / SSD1680 | 引脚已按 8 针接口定 |
| 麦克风方案 | ✅ ICS-43434（数字 I2S） | 15–17 确定；无声卡增益调节，靠软件 |
| 功放型号 | ✅ MAX98357A | 41 接 SD 使能 |
| 扬声器 | ✅ 8 Ω 1 W | 3.3 V 下约 0.4 W，音量够用 |
| SD 卡 | ✅ 不装 | 13/14 释放；存储改用片内 Flash |
| 局部刷新残影 | ⏳ 待实测 | 决定"每 N 次局部刷做一次全刷"的 N |
| 麦克风供电门控 | ⏳ 待定 | 若加 P-MOS，42 保留；不加则释放 42 |

## 七、与固件的对应

以上编号将原样写入 `firmware/main/board_config.h`，做成统一的宏定义，禁止在组件里直接写数字。骨架大致如下：

```c
#pragma once

/* 墨水屏 —— SPI2 (FSPI)，SSD1680 128x296 */
#define PIN_EPD_SCK    12
#define PIN_EPD_MOSI   11
#define PIN_EPD_CS     10
#define PIN_EPD_DC      9
#define PIN_EPD_RST     8
#define PIN_EPD_BUSY    7

/* 存储：词库音频编进 Flash 分区，记录走 NVS，不使用 SD */

/* 音频输入 I2S0 */
#define PIN_MIC_BCLK   15
#define PIN_MIC_WS     16
#define PIN_MIC_DIN    17

/* 音频输出 I2S1 */
#define PIN_SPK_BCLK   18
#define PIN_SPK_WS     21
#define PIN_SPK_DOUT   39

/* 按键（低有效） */
#define PIN_KEY_PREV    2
#define PIN_KEY_NEXT    4
#define PIN_KEY_OK      5
#define PIN_KEY_BACK   40

/* 模拟与电源 */
#define PIN_BAT_ADC     1
#define PIN_AMP_EN     41   /* MAX98357 SD */
#define PIN_MIC_PWR_EN 42   /* 可选：麦克风负载开关 */
```

## 八、变更记录

| 日期 | 变更 |
|---|---|
| 2026-11-07 | 初版：确定 21 个功能引脚，排除 33–37 与 strapping 引脚 |
| 2026-11-07 | 器件确定：SSD1680 128×296、ICS-43434、MAX98357A、8 Ω 1 W |
| 2026-11-07 | 取消 SD 卡：改为片内 Flash + NVS，释放 GPIO13/14 |
