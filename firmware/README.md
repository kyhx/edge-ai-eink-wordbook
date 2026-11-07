# firmware — 设备端固件

ESP32-S3 上的全部运行代码：墨水屏驱动、按键输入、音频输入输出、端侧推理、复习调度与无线同步。

## 工具链

默认 **ESP-IDF v5.x**，构建命令 `idf.py build` / `idf.py flash monitor`。

Arduino-ESP32 仅作备选：一旦改用，`CMakeLists.txt` 与组件划分需要相应调整，并在对应 Issue 中记录原因，避免两套构建方式混用。

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
│   └── net/             # Wi-Fi 配网与学习记录同步
├── models/              # 由 ml/ 导出、供 infer 读取的模型文件（大文件慎入）
└── CMakeLists.txt
```

## 音频参数（初步，随 #3 定稿）

| 参数 | 取值 | 说明 |
|---|---|---|
| 采样率 | 16 kHz | 语音识别/发音评价的常用值，兼顾算力与带宽 |
| 位深 | 16 bit | |
| 声道 | 单声道 | 只取一个麦克风通道 |
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

未开始。对应任务 [#1](https://github.com/kyhx/edge-ai-eink-wordbook/issues/1)（2026.11.07 – 11.14）：搭建 ESP-IDF 环境，调通墨水屏、按键、音频采集与播放。
