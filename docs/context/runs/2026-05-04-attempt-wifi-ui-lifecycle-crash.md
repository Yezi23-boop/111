---
id: attempt-2026-05-04-wifi-ui-lifecycle-crash
tags: wifi, lvgl, crash, attempt-log
summary: wifi-ui-lifecycle-crash；结果：success。
last_reviewed: 2026-05-04
memory_type: episodic
scope: task
owners: main/ui/custom/wifi_management_controller.c, docs/context/knowledge/project/wifi-management-ui-behavior.md
triggers: wifi 管理页 二次进入 crash lifecycle LoadProhibited
evidence_level: observed
---

# Attempt Log: wifi-ui-lifecycle-crash

## 背景

- 本次要验证什么：记录 Wi-Fi 管理页二次进入崩溃的已验证修复路径，避免后续只清空局部按钮或重复猜测 UI 问题。
- 对应任务或计划：Wi-Fi 管理页 lifecycle crash 修复
- 结果状态：success

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：未涉及或未记录
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- main/ui/custom/wifi_management_controller.c
- docs/context/knowledge/project/wifi-management-ui-behavior.md
- 执行的命令或动作：
- 确认 Wi-Fi 管理页返回主界面时使用 lv_screen_load_anim(..., true) 删除旧 screen
- 在二次进入前检查 lv_obj_is_valid(s_screen)
- 通过 LV_EVENT_DELETE 清空 screen 与全部子控件缓存指针
- 已尝试但不应直接重复的路径：
- 不要只判断静态指针非空就复用 LVGL 对象
- 不要只清空某几个按钮缓存而保留旧 screen 指针

## 观测

- 关键日志/证据：
- panic/backtrace 指向 lv_style_get_prop 内 LoadProhibited
- wifi-management-ui-behavior.md 已记录 LV_EVENT_DELETE + lv_obj_is_valid 边界
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：根因是静态缓存仍指向已被 LVGL 删除的对象；修复应围绕对象生命周期，而不是继续堆 delay 或重建局部控件。
- 仍然不能确认的事实：
- 不同页面切换动画参数下是否还有同类悬空对象风险

## 未验证风险

- 下一轮仍需补证据的边界：
- 新 hand-written LVGL 页面若使用自动删除旧 screen，应同步加 delete 清缓存与 valid guard
