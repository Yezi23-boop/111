---
id: attempt-2026-08-01-ota
tags: context, run, attempt-log
summary: OTA 固件只保留云端发布路径；结果：success。
last_reviewed: 2026-08-01
memory_type: episodic
scope: task
status: active
result: success
owners: main/services/ota/ota_board_test.c; main/Kconfig.projbuild; main/CMakeLists.txt; main/services/ota/ota_test_ca.pem; tools/ota_host/README.md
triggers: OTA 固件只保留云端发布路径
evidence_level: observed
record_reasons: route-choice, evidence
force_reason: 
---

# Attempt Log: OTA 固件只保留云端发布路径

## 背景

- 本次要验证什么：OTA 固件只保留云端发布路径
- 对应任务或计划：未绑定计划
- 结果状态：success
- 长期记录理由：route-choice, evidence

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：未涉及或未记录
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- main/services/ota/ota_board_test.c; main/Kconfig.projbuild; main/CMakeLists.txt; main/services/ota/ota_test_ca.pem; tools/ota_host/README.md
- 执行的命令或动作：
- 删除设备端 LAN manifest、局域网测试 CA 和本地 OTA 配置；板测固定调用云端 manifest
- 已尝试但不应直接重复的路径：
- 不要再为设备端保留局域网 HTTPS OTA 分支或内嵌测试 CA

## 观测

- 关键日志/证据：
- ota source tests 6 passed、ota host tests 5 passed、idf.py fullclean/build 通过；sdkconfig 正常配置中 board test 关闭
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：未形成稳定结论
- 仍然不能确认的事实：
- 未记录

## 未验证风险

- 下一轮仍需补证据的边界：
- 设备升级统一使用 watch.934000.xyz 云端 manifest/artifact
