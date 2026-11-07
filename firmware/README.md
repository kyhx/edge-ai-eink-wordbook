# firmware — 设备端固件

ESP32-S3 上的全部运行代码：墨水屏驱动、按键输入、音频输入输出、端侧推理、复习调度与无线同步。

## 工具链

使用 **ESP-IDF v6.1**，构建命令 `idf.py build` / `idf.py flash monitor`。

Arduino-ESP32 仅作备选：一旦改用，`CMakeLists.txt` 与组件划分需要相应调整，并在对应 Issue 中记录原因，避免两套构建方式混用。

本机安装位置：`F:\esp\v6.1\esp-idf`，工具链在 `C:\Users\Administrator\.espressif`。

## 快速开始

```bat
:: 1. 激活 IDF 环境（每个新终端都要执行一次）
call F:\esp\v6.1\esp-idf\export.bat

:: 2. 进入工程
cd /d C:\Users\Administrator\Desktop\paper\firmware

:: 3. 配置目标芯片（只需一次，会生成 sdkconfig）
idf.py set-target esp32s3

:: 4. 编译
idf.py build

:: 5. 烧录并看日志（串口见设备管理器，当前为 COM4）
idf.py -p COM4 flash monitor
```

按键 `Ctrl+]` 退出 monitor。若中文日志出现乱码，在终端先执行 `chcp 65001` 并设置 `PYTHONUTF8=1`。

## 规划结构

```
firmware/
├── main/                # 主程序、状态机、任务调度
│   └── board_config.h   # 引脚定义集中在此，不散落到各模块
├── components/
│   ├── display/         # 墨水屏驱动：全刷、局部刷新、残影处理
│   ├── input/           # 按键/触摸：GPIO 中断、软件消抖、长按短按
│   ├── audio_in/        # I2S 麦克风采集、端点检测（VAD）
│   ├── audio_out/       # 单词标准音播放
│   ├── infer/           # 特征提取 + 量化模型推理
│   ├── review/          # 词库与间隔重复调度
│   ├── net/             # Wi-Fi 配网与学习记录同步
│   └── storage/         # Flash 词库音频分区与 NVS 读写（不含 SD 卡）
├── models/              # 由 ml/ 导出、供 infer 读取的模型文件（大文件慎入）
└── CMakeLists.txt
```

## 存储

不使用 SD 卡。单词音频编译进独立的 Flash 分区（只读），学习记录与复习进度存 NVS。没有任何依赖可移动介质的代码路径，也不引入 FATFS。

## 音频参数（初步，随 #3 定稿）

| 参数 | 取值 | 说明 |
|---|---|---|
| 采样率 | 16 kHz | 语音识别/发音评价的常用值，兼顾算力与带宽 |
| 位深 | 采集 24 bit（I2S 32 bit 槽），入模型前转 16 bit | ICS-43434 原生 24 bit，量化模型按 int8/16 bit 处理 |
| 声道 | 单声道 | ICS-43434 的 WS 接地取左声道 |
| 帧长 / 帧移 | 待定（初拟 25 ms / 10 ms） | 由 `ml/configs/` 确定后回填此处 |

特征维度、mel 滤波器个数等由 `ml/` 训练侧决定；固件侧必须与之同版本，否则推理结果无效。

## 交付物位置

| 内容 | 存放位置 |
|---|---|
| 可编译、可烧录的工程 | `firmware/` |
| 接线表（引脚—外设对照） | `hardware/wiring/` |
| 实物照片、接线照片 | `docs/figures/` |
| 刷新耗时、信噪比等实测记录 | `docs/logs/` 或对应 Issue 评论 |

## 约定

- 分支开发，合并前保证 `idf.py build` 通过；提交信息写清影响的外设。
- 引脚定义只写在 `main/board_config.h`。
- 模型文件如超过 5 MB，改用 Release 附件或 Git LFS。
- 关键参数（采样率、帧长、特征维度）在 `docs/`、`ml/`、本文件三处保持一致并注明版本。

## 当前状态

**工具链与烧录链路已打通（2026-11-07）**，对应 [#1](https://github.com/kyhx/edge-ai-eink-wordbook/issues/1)。自检程序 `main/main.c` 已烧录运行，实测输出：

```
boot.esp32s3: SPI Mode QIO / SPI Flash Size 16MB
octal_psram: Found 8MB PSRAM device, Speed 80MHz
cpu_start: cpu freq: 240000000 Hz
selftest: 芯片 ESP32-S3, 2 核, 版本 v0.2
selftest: Flash 16 MB / PSRAM 8192 KB
selftest: 内部RAM 空闲 380 KB / 总 429 KB
selftest: 按键 PREV=1 NEXT=1 OK=1 BACK=1
```

说明：N16R8 的 Octal PSRAM 被正确识别（8 MB @ 80 MHz，内存测试通过），Flash 16 MB，主频 240 MHz，按键 GPIO 内部上拉生效。

尚未完成：墨水屏驱动、I2S 录音与播放。
