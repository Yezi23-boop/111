---
id: attempt-2026-08-02-music-lvgl-back-screen-watchdog
tags: music, lvgl, watchdog
summary: music-lvgl-back-screen-watchdog；结果：partial。
last_reviewed: 2026-08-02
memory_type: episodic
scope: task
status: active
result: partial
owners: main/ui/custom/music_controller.c, tests/test_music_ui_source.py
triggers: lv_obj_update_layout, IDLE1 task watchdog
evidence_level: observed
record_reasons: error-signature, evidence
force_reason: 
---

# Attempt Log: music-lvgl-back-screen-watchdog

## 背景

- 本次要验证什么：修复真实音乐起播后离开音乐页触发的 LVGL task watchdog
- 对应任务或计划：2026-07-31 AI Memory Watch 在线音乐播放执行计划
- 结果状态：partial
- 长期记录理由：error-signature, evidence

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：ESP32-S3 COM7
- 关键前置条件：真实 MP3 解码已打开且 music_player 已取得音频输出 session

## 操作

- 修改过的文件或 owner：
- main/ui/custom/music_controller.c
- tests/test_music_ui_source.py
- 执行的命令或动作：
- 未记录
- 已尝试但不应直接重复的路径：
- 不要通过延长/关闭 task watchdog 掩盖 LVGL 对象生命周期错误

## 观测

- 关键日志/证据：
- 附件日志：IDLE1 task watchdog，CPU1=lvgl_task，回溯 lv_obj_update_layout -> lv_display_refr_timer
- 音乐 source tests 17/17、idf.py build、COM7 app-flash 通过
- board_logs/2026-08-02-20-55-50-music-back-screen-lifecycle.log：60 秒无 panic/WDT，未覆盖真实播放后返回操作
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：返回路径在启动 screen 动画后立即删除当前 screen，动画继续引用已删除对象并使 LVGL 布局遍历卡死；同步切屏后再销毁 view 可消除该生命周期冲突
- 仍然不能确认的事实：
- 未记录

## 未验证风险

- 下一轮仍需补证据的边界：
- 真机播放一首歌曲后按返回，观察至少一个 watchdog 窗口
