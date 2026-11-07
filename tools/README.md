# tools — 辅助脚本

围绕数据采集、标注、评测与固件调试的小工具。一次性脚本也放这里，用完标注清楚。

## 规划内容

| 脚本 | 作用 |
|---|---|
| `record.py` | 按词库顺序引导录音，自动命名并写入 `data/raw/` |
| `annotate.py` | 简易标注界面，逐条播放并写入 `data/labels.csv` |
| `split.py` | 生成训练/验证/测试划分清单到 `data/splits/` |
| `check_dataset.py` | 校验采样率、时长、缺失文件与标签分布 |
| `bench_latency.py` | 统计模型在 PC 与设备上的单帧推理耗时 |
| `serial_log.py` | 串口日志抓取与时间戳对齐，便于功耗/耗时分析 |

## 约定

- Python 脚本附 `argparse` 帮助信息，`python xxx.py -h` 可直接看懂用法。
- 路径参数默认指向仓库内相对路径，不写死本机绝对路径。
- 依赖统一列在根目录 `requirements-dev.txt`；能只用标准库的尽量不用第三方。
- 会改写 `data/` 中受版本管理文件的脚本，必须支持 `--dry-run`。

## 当前状态

未开始。最先需要的是 `record.py` 与 `check_dataset.py`。
