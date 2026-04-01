<INSTRUCTIONS>
## Scope

- 本目录负责 `traffic_inference` 推理、滑窗、后处理、实时麦克风接入和 `traffic_audio` runtime。
- 这里同时包含模型集成、实时输入契约和产品侧告警逻辑。

## Read First

- 根目录 `AGENTS.md`
- `boards/esp32-s3-touch-amoled-2.06/AGENTS.md`
- `docs/context/knowledge/project/traffic-inference-realtime-mic.md`
- `docs/context/knowledge/project/traffic-inference-postprocess-v1.md`
- `docs/context/knowledge/project/traffic-inference-threshold-selection.md`
- `docs/context/knowledge/project/traffic-audio-feature-manager.md`

## Do

- 先确认改动落在模型 runner、滑窗、后处理、实时输入还是 `traffic_audio` runtime。
- 保持 `24 kHz / 2 ch / "MR" -> 16 kHz mono` 的 realtime 输入契约稳定，除非任务明确要求修改并给出验证方案。
- 修改阈值、label 归一化、debounce、alert dispatch 或滑窗状态时，说明对误报/漏报和现有日志契约的影响。
- 涉及大状态、环形缓冲或滑窗对象时，明确说明是否继续依赖 `PSRAM`。

## Do Not

- 不要把 demo 验证路径和正式 runtime 路径混成一个模糊入口。
- 不要在未说明原因时同时改模型阈值、postprocess 语义和 realtime 输入契约。
- 不要忽略 `background`、`horn`、`siren` 的现有标签约定和应用侧 alert 回调边界。

## Validation

- 至少运行 `idf.py build`。
- 若改推理契约或后处理，优先说明相关 Python 合约测试是否需要一起跑。
- 最小验证应说明：
  - 编译通过
  - realtime / postprocess / runtime 入口无明显接口回归
  - 若涉及板端行为，明确是否做了 `flash` / `monitor` 或仅源码级验证

## Escalation

- 若改 realtime 音频契约、阈值策略、PSRAM 依赖、feature-manager 边界或应用侧 alert 语义，需同步更新项目知识文档。
</INSTRUCTIONS>
