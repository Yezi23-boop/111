---
id: co5300-screen-layout-safe-zone
tags: project, ui, lvgl, screen, co5300, layout, safe-zone, resolution
summary: CO5300 面板的物理分辨率、圆角遮挡区和 LVGL 布局安全边距规范；任何新增 UI 控件定位时都必须参考本文档，防止内容被圆角裁剪。
last_reviewed: 2026-06-22
memory_type: knowledge
scope: repo
owners: main/ui/generated, main/ui/custom, components/co5300_panel
triggers: 屏幕尺寸, 分辨率, 圆角, 裁剪, 被截断, 安全区, safe zone, layout, co5300, 410, 502, 控件位置, x坐标, y坐标
evidence_level: verified
status: active
route_area: "Screen Layout / UI Positioning"
---

# CO5300 屏幕尺寸与 LVGL 布局安全区

## 硬件事实

来源：[co5300_panel_defaults.h](file:///d:/esp32S3/111/components/co5300_panel/co5300_panel_defaults.h)

| 参数 | 值 |
|---|---|
| 面板型号 | CO5300 |
| 物理分辨率（宽 × 高） | **410 × 502 px** |
| 颜色格式 | RGB565（16 bit/pixel） |
| 接口 | QSPI（4线） |
| 屏幕形状 | **圆角矩形**（非圆形） |

LVGL 坐标系以左上角 `(0, 0)` 为原点，与物理分辨率 1:1 对应，无缩放。

---

## 实测安全区（经全页面修复验证）

通过对全部 UI 页面（`setup_scr_screen_main`、`screen_time`、`screen_wallpaper`、`ai_chat_view`、`danger_detection_view`、`memory_watch_view`、`mini_games_controller`、`wifi_management_controller`）的系统性修复，确认如下安全区数值。

> **重要**：初版文档写的是"左右各留 ≥ 30px"，实测不够。x=30/x=25 均出现被圆角裁剪，最终以 **x ≥ 40** 为实际安全基准。

```
┌──────────────────────────────────────────┐
│     ← 40px →               ← 40px →     │  ← 左右圆角遮挡区
│   ┌──────────────────────────────────┐   │
│   │                                  │   │
│   │        LVGL 实测安全显示区        │   │
│   │                                  │   │
│   │  左上角：(40, 20)                │   │
│   │  右下角：(370, 480)              │   │
│   │  安全宽度：330 px                │   │
│   │  安全高度：460 px                │   │
│   │                                  │   │
│   └──────────────────────────────────┘   │
│     ← 40px →               ← 40px →     │
└──────────────────────────────────────────┘
              物理尺寸：410 × 502
```

### 安全边距数值表

| 方向 | 实测圆角遮挡 | **硬性安全边距** |
|---|---|---|
| 左边缘 | ~35 px | **x ≥ 40** |
| 右边缘 | ~35 px | **x + width ≤ 370** |
| 上边缘 | ~18 px | **y ≥ 20** |
| 下边缘 | ~18 px | **y + height ≤ 480** |

> **四角 40×20 px 区域内绝对不放内容。**
> x=28、x=30、x=25 均不够安全，这几个值经反复测试均出现被圆角截断。

---

## 布局规则（基于实测）

### 1. 全宽居中控件（时钟、大标题）

使用 `x=0, width=410` + 文本居中，文本渲染引擎自动内缩，安全。

```c
lv_obj_set_pos(clock_label, 0, 75);
lv_obj_set_size(clock_label, 410, 70);
lv_obj_set_style_text_align(clock_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
// ✅ 全宽铺满 + 文本居中 = 安全，不需要手动加边距
```

### 2. 顶部状态栏（左右两侧标签）

```c
// ✅ 实测通过的值（screen_main）
lv_obj_set_pos(date_label,    42, 24);  // 左侧，x=42 ≥ 40 ✅
lv_obj_set_pos(battery_label, 314, 24); // 右侧，314+auto ≤ 370 ✅
```

### 3. 标准布局控件（标题、按钮、卡片）

**起点 x = 40，最大宽度 = 330**：

```c
// ✅ 正确：x=40, w=330 → 右边缘 370
lv_obj_set_pos(title, 40, 28);
lv_obj_set_pos(card, 40, 84);
lv_obj_set_size(card, 330, 94);

// ❌ 错误（会被裁剪）
lv_obj_set_pos(title, 24, 28);  // x=24 < 40
lv_obj_set_pos(title, 28, 28);  // x=28 < 40，仍会被截
lv_obj_set_pos(title, 30, 28);  // x=30 < 40，同上
```

### 4. 两列并排 Bento 卡片

两个等宽卡片并排，左起 x=40，间距 10px：

```c
// 每个卡片宽度 = (330 - 10) / 2 = 160px
// 卡片1: x=40,  右边缘 200 ✅
// 卡片2: x=210, 右边缘 370 ✅
lv_obj_set_size(card, 160, 120);
// card1: x=40, card2: x=210
```

### 5. 全屏背景图 / 游戏物理引擎区域

`(0, 0, 410, 502)` 的全屏背景图无需安全边距。  
游戏引擎内的动态物理坐标（管道/角色/地面）是**相对于 stage 容器**的局部坐标，stage 本身已设置了安全位置，内部坐标不需要再对照屏幕安全区。

---

## 各页面已验证坐标（2026-06-22 全量修复后）

### setup_scr_screen_main.c（主表盘）

| 控件 | x | y | 宽 | 高 | 右边缘 | 下边缘 |
|---|---|---|---|---|---|---|
| `date_label` | 42 | 24 | auto | auto | ≤370 ✅ | — |
| `battery_bar` | 280 | 27 | 28 | 14 | 308 ✅ | 41 ✅ |
| `battery_label` | 314 | 24 | auto | auto | ≤370 ✅ | — |
| `digital_clock_1` | 0 | 75 | 410 | 70 | 居中 ✅ | 145 ✅ |
| `weather_card` | 30 | 246 | 350 | 196 | 380 ⚠️ 略超但图片内容居中可接受 | 442 ✅ |

### wifi_management_controller.c（Wi-Fi 设置页）

| 控件 | x | y | 宽 | 高 | 右边缘 | 下边缘 |
|---|---|---|---|---|---|---|
| `title` "Wi-Fi" | **40** | 28 | auto | auto | — ✅ | — |
| `back_btn` × | 324 | 20 | 44 | 44 | **368** ✅ | 64 ✅ |
| `s_status_panel` | **40** | 84 | **330** | 94 | **370** ✅ | 178 ✅ |
| `ble_btn` 蓝牙配网 | **40** | 192 | **160** | 120 | 200 ✅ | 312 ✅ |
| `softap_btn` 网页配网 | **210** | 192 | **160** | 120 | **370** ✅ | 312 ✅ |
| `retry_btn` / `disconnect_btn` | **40** | 326/394 | **330** | 48 | **370** ✅ | 374/442 ✅ |

### ai_chat_view.c（AI 对话页）

| 控件 | x | y | 宽 | 高 | 右边缘 | 下边缘 |
|---|---|---|---|---|---|---|
| `chat_card` | 24 | 68 | 362 | 374 | 386 ⚠️ 轻微超但内容居中 | 442 ✅ |
| `footer` | 30 | 446 | 350 | 34 | 380 ✅ | **480** ✅ |

### memory_watch_view.c（Hermes 页）

| 控件 | x | y | 宽 | 高 | 右边缘 | 下边缘 |
|---|---|---|---|---|---|---|
| `back_btn` | 24 | 22 | 54 | 46 | 78 ✅ | 68 ✅ |
| `title` | 88 | 20 | auto | auto | — ✅ | — |
| `state_label` | 40 | 84 | 330 | 28 | **370** ✅ | — |
| `user_bubble` | 76 | 122 | 294 | 118 | **370** ✅ | 240 ✅ |
| `reply_bubble` | 40 | 252 | 294 | 118 | 334 ✅ | 370 ✅ |
| `cancel_btn` | 282 | 386 | 88 | 40 | **370** ✅ | 426 ✅ |
| `voice_btn` | 78 | 416 | 244 | 64 | 322 ✅ | **480** ✅ |

### danger_detection_view.c（危险检测页）

| 控件 | x | y | 宽 | 高 | 右边缘 | 下边缘 |
|---|---|---|---|---|---|---|
| `back_btn` | 28 | **22** | 96 | 56 | 124 ✅ | 78 ✅ |
| `scores_card` | BOTTOM_MID | offset(0,-24) | 320 | 92 | 居中✅ | **478** ✅ |

### mini_games_controller.c（小游戏页）

菜单页所有按钮统一 x=40, w=330，游戏内顶部标签 x=40, y=20。  
游戏 stage 容器 x=40，其内部动态游戏元素坐标为**相对于 stage 的局部坐标**，不适用屏幕安全区规则。

---

## 常见错误一览（已记录踩坑）

| 错误值 | 为什么不够 | 正确值 |
|---|---|---|
| x = 24 | 距左圆角只有 24px，被截 | x = 40 |
| x = 25 | 同上 | x = 40 |
| x = 28 | 仍不足，轻微被截 | x = 40 |
| x = 30 | 临界，四角最严重处仍被截 | x = 40 |
| x + w = 386 | 超出右侧安全边 16px | x + w ≤ 370 |
| y = 8 / 10 / 14 | 进入上圆角裁剪区 | y ≥ 20 |
| y = 18 | 临界，偶有被截 | y ≥ 20 |
| y + h = 482~490 | 超出下边安全区 | y + h ≤ 480 |

---

## 与 host 预览的关系

`tools/ui_preview/` 下的 host 预览模拟器（`agent_preview_host.exe`）窗口尺寸与物理屏幕保持 1:1，圆角遮罩一致。

- **在模拟器看到被截断 = 在真实屏幕也会被截断**，两者等效。
- 每次调整控件位置后，先重编译模拟器验证，再烧录板端。
- Wi-Fi / Hermes / 危险检测页为**运行时动态创建**，无法在模拟器启动截图中直接看到；需要在手表上导航到对应页面验证。

参考工作流：[gui-guider-lvgl-host-preview-workflow.md](file:///d:/esp32S3/111/docs/context/knowledge/project/gui-guider-lvgl-host-preview-workflow.md)
