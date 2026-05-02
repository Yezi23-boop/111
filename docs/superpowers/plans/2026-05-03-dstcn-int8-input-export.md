# DS-TCN 纯 INT8 输入导出计划

## 目标

把当前 `AudioClassification-Pytorch` 的 `DS-TCN-small` 导出链路，从
“`FLOAT input -> QuantizeLinear -> INT8 主图`”收紧为“`INT8 input -> INT8 主图`”，
尽量减少板端入口的浮点路径和额外量化开销，同时保持现有模型精度与部署兼容性。

## 已知事实

- 官方 ESP-DL 文档建议板端按模型输入 exponent 先量化输入，再送入模型。
- 当前 `esp_ppq==1.2.10` 的 `espdl_quantize_torch()` / `espdl_quantize_onnx()` 默认把
  `input_dtype` 固定为 `torch.float32`，并在导出图入口插入 `QuantizeLinear`。
- 当前 `V3/V3.1` 的 `.info` 均显示：
  - 图输入是 `FLOAT`
  - 入口存在 `Squeeze -> Unsqueeze -> QuantizeLinear`
  - 后续主体算子已经是 `ESPDL_S3_INT8`

## 方案

1. 先补一个轻量检查脚本，明确判定当前导出物是否仍为 `FLOAT` 入口。
2. 在导出脚本中增加“导出后图入口压缩”步骤：
   - 识别图首部的 `QuantizeLinear`
   - 读取其 scale / zero-point，推导输入 exponent
   - 将模型输入改写为 `INT8`
   - 去掉入口 `QuantizeLinear` 及仅服务于该路径的 `Squeeze/Unsqueeze`
   - 保持后续量化主图不变
3. 重新导出 `.espdl`
4. 用 `.info/.json` 检查：
   - 输入 dtype 是否变为 `INT8`
   - 入口 `QuantizeLinear` 是否消失
   - 主体热点算子映射是否保持 `ESPDL_S3_INT8`
5. 如导出成功，再做样板工程 `build/flash/monitor` 闭环，比较：
   - 输入 dtype 日志
   - RAM / Flash
   - 推理耗时

## 验收标准

- `.info` 图输入变为 `INT8`
- 图入口不再出现 `FLOAT -> QuantizeLinear` 边界
- 样板工程 `Model::test()` 通过
- 不破坏现有推理结果
- 若耗时下降，则记录收益；若无明显收益，也记录结论，避免后续重复踩坑

## 风险与回滚

- 风险 1：`esp_ppq` 导出器默认假定 float 入口，图入口强改后可能导致 `.espdl` 结构不兼容。
  - 处理：保留原导出物，新增候选版本，不覆盖当前 active 版本。
- 风险 2：去掉 `Squeeze/Unsqueeze` 后输入布局不一致。
  - 处理：严格以 `.info` shape 和样板工程 `get_shape()` 日志为准。
- 风险 3：板端耗时未下降。
  - 处理：保留结论，停止继续在这条路径上投入，转入更纯 `DS-CNN` 候选。
