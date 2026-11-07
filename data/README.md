# data — 词库与标注数据

存放**体积小、需要版本管理**的数据：词库、标注表、数据划分清单。

原始录音（`.wav`）体积大，不入库，放在本地 `data/raw/`（已忽略）；需要共享时通过 Release 附件或网盘分发，并在 `docs/dataset/` 记录获取方式与校验值。

## 目录规划

| 位置 | 内容 | 是否入库 |
|---|---|---|
| `data/vocabulary.csv` | 词库（≥50 词条）：单词、音标、释义、例句、音频文件名 | ✅ |
| `data/labels.csv` | 标注表：样本 ID、单词、说话人、标签、标注人、备注 | ✅ |
| `data/splits/` | 训练/验证/测试划分清单（按样本 ID） | ✅ |
| `data/raw/` | 原始录音 | ❌ 已忽略 |
| `data/processed/` | 重采样、去静音后的中间音频 | ❌ 已忽略 |

## 数据格式约定

**vocabulary.csv**

```csv
word,phonetic,meaning,example,audio
apple,ˈæpl,苹果,An apple a day keeps the doctor away.,apple.wav
```

**labels.csv**

```csv
sample_id,word,speaker_id,label,annotator,note
s001,apple,spk03,1,kyhx,清晰
s002,apple,spk05,0,kyhx,尾音吞音
```

- `label`：`1` = 发音正确，`0` = 需要改进。
- 采样率、位深、声道等音频参数统一记录在 `docs/dataset/` 的说明文件中。

## 当前状态

未开始。目标：≥50 词条、≥200 条标注语音（2026.11.14 – 11.28）。
