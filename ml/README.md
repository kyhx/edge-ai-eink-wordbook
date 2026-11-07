# ml — 训练与量化

离线侧的全部 Python 代码：从原始语音到可烧录模型。固件只消费本目录导出的产物。

## 规划结构

```
ml/
├── configs/            # 数据划分、模型超参、量化参数（yaml）
├── features/           # 梅尔频谱、MFCC、基频提取
├── datasets/           # 数据加载、增强、训练/验证/测试划分
├── models/             # 轻量二分类网络定义
├── train.py            # 训练入口
├── evaluate.py         # 准确率、P/R/F1、与人工标注一致度
├── quantize.py         # 训练后量化（int8）
├── export.py           # 导出 .tflite / C 数组给固件
├── baseline/           # 规则法：基于基频的对照实现
└── requirements.txt
```

## 约定

- 每次实验固定随机种子并写入 `configs/`，结果记入 `runs/{日期}-{简述}/metrics.json`。
- 数据划分只在一处生成（`datasets/split.py`），训练与评测共用，禁止各自重新划分。
- 权重与导出模型不入库（见根目录 `.gitignore`），只提交配置、脚本与指标。
- 规则法与模型法必须跑**同一份测试集**，对比表统一由 `evaluate.py` 输出。

## 当前状态

未开始。前置依赖：`data/` 中 ≥200 条标注语音。
