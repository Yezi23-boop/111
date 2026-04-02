# srp-defense - Design Spec

## I. Project Information

| Item | Value |
| ---- | ----- |
| **Project Name** | srp-defense |
| **Canvas Format** | PPT 16:9 (1280x720) |
| **Page Count** | 9 |
| **Design Style** | Academic defense with general consulting structure |
| **Target Audience** | 校级 SRP 结题答辩评委、指导教师、学院师生 |
| **Use Case** | 校级大学生创新创业训练计划项目（SRP）结题答辩 |
| **Created Date** | 2026-04-02 |

---

## II. Canvas Specification

| Property | Value |
| -------- | ----- |
| **Format** | PPT 16:9 |
| **Dimensions** | 1280x720 |
| **viewBox** | `0 0 1280 720` |
| **Margins** | Left/Right 40px, Top 0px, Bottom 35px |
| **Content Area** | x: 40-1240, y: 135-650 |

---

## III. Visual Theme

### Theme Style

- **Style**: Academic defense with structured engineering narration
- **Theme**: Light theme
- **Tone**: 正式、理性、工程化、适合校内答辩

### Color Scheme

| Role | HEX | Purpose |
| ---- | --- | ------- |
| **Background** | `#FFFFFF` | Page background |
| **Secondary bg** | `#EAF1FB` | Card background, highlight band |
| **Primary** | `#1F4E79` | Header, title emphasis, main cards |
| **Accent** | `#D97A32` | Key highlights, step numbers, warning accents |
| **Secondary accent** | `#5B8FD9` | Diagram connectors, secondary title bars |
| **Body text** | `#233142` | Main body text |
| **Secondary text** | `#5B6572` | Descriptions, annotations |
| **Tertiary text** | `#8D97A5` | Footer, page notes |
| **Border/divider** | `#C9D7E8` | Card borders, divider lines |
| **Success** | `#2E8B57` | Completed / validated indicators |
| **Warning** | `#C44F2A` | Risks / pending items |

### Gradient Scheme (if needed, using SVG syntax)

```xml
<linearGradient id="titleGradient" x1="0%" y1="0%" x2="100%" y2="100%">
  <stop offset="0%" stop-color="#1F4E79"/>
  <stop offset="100%" stop-color="#5B8FD9"/>
</linearGradient>

<radialGradient id="bgDecor" cx="82%" cy="18%" r="52%">
  <stop offset="0%" stop-color="#5B8FD9" stop-opacity="0.16"/>
  <stop offset="100%" stop-color="#5B8FD9" stop-opacity="0"/>
</radialGradient>
```

---

## IV. Typography System

### Font Plan

**Recommended preset**: P2/P1 hybrid

| Role | Chinese | English | Fallback |
| ---- | ------- | ------- | -------- |
| **Title** | 黑体 | Arial Bold | Microsoft YaHei |
| **Body** | 微软雅黑 | Arial | sans-serif |
| **Code** | - | Consolas | Monaco |
| **Emphasis** | 黑体 | Arial Bold | Microsoft YaHei |

**Font stack**: `"Microsoft YaHei", "微软雅黑", Arial, sans-serif`

### Font Size Hierarchy

**Baseline**: Body font size = 18px

| Purpose | Ratio | 24px baseline (relaxed) | 18px baseline (dense) | Weight |
| ------- | ----- | ---------------------- | -------------------- | ------ |
| Cover title | 2.5-3x | 60-72px | 48-54px | Bold |
| Chapter title | 2-2.5x | 48-60px | 38-44px | Bold |
| Content title | 1.5-2x | 36-48px | 28-34px | Bold |
| Subtitle | 1.2-1.5x | 29-36px | 22-26px | SemiBold |
| **Body content** | **1x** | **24px** | **18px** | Regular |
| Annotation | 0.75-0.85x | 18-20px | 14-15px | Regular |
| Page number/date | 0.55-0.65x | 13-16px | 11-12px | Regular |

---

## V. Layout Principles

### Page Structure

- **Header area**: 70px, dark blue title bar with red-accent edge, inherited from academic template
- **Content area**: 515px, card-based structured engineering presentation
- **Footer area**: 55px, source / section / page number

### Common Layout Modes

| Mode | Suitable Scenarios |
| ---- | ----------------- |
| **Single column centered** | Cover, conclusion statements |
| **Left-right split (5:5)** | Architecture, comparison, image-text evidence |
| **Left-right split (4:6)** | Image + explanation, screenshot evidence |
| **Top-bottom split** | Background + significance, process explanation |
| **Three/four column cards** | Goals, results, innovation points |
| **Matrix grid** | Module overview, achievements and risks |

### Spacing Specification

| Element | Recommended Range | Current Project |
| ------- | ---------------- | --------------- |
| Card gap | 20-32px | 22px |
| Content block gap | 24-40px | 26px |
| Card padding | 20-32px | 22px |
| Card border radius | 8-16px | 10px |
| Icon-text gap | 8-16px | 12px |
| Single-row card height | 530-600px | 515px |
| Double-row card height | 265-295px each | 244px |
| Three-column card width | 360-380px each | 378px |

---

## VI. Icon Usage Specification

### Source

- **Built-in icon library**: `templates/icons/`
- **Usage method**: Placeholder format `{{icon:category/icon-name}}`

### Recommended Icon List

| Purpose | Icon Path | Page |
| ------- | --------- | ---- |
| Project carrier | `{{icon:devices/smartwatch}}` | Slide 01 / 03 |
| Embedded core | `{{icon:devices/microchip}}` | Slide 03 / 04 |
| Network service | `{{icon:devices/wifi}}` | Slide 04 / 06 |
| Audio sensing | `{{icon:media/microphone}}` | Slide 04 / 05 |
| Alert feedback | `{{icon:communication/megaphone}}` | Slide 05 / 06 |
| AI dialogue | `{{icon:communication/comments}}` | Slide 04 / 06 |
| Validation status | `{{icon:status/shield-check}}` | Slide 08 |

---

## VII. Chart Reference List

| Chart Type | Reference Template | Used In |
| ---------- | ------------------ | ------- |
| process_flow | `templates/charts/process_flow.svg` | Slide 05 |

---

## VIII. Image Resource List

| Filename | Dimensions | Ratio | Purpose | Type | Status | Generation Description |
| -------- | --------- | ----- | ------- | ---- | ------ | --------------------- |
| ai-dialog.png | 1024x1536 | 0.67 | AI 对话页面竖版界面展示 | Photography | Existing | 用户提供的 UI 截图，适合放在左右分栏中展示 AI 对话界面 |
| home-ui.png | 1024x1536 | 0.67 | 主界面竖版界面展示 | Photography | Existing | 用户提供的 UI 截图，适合放在左右分栏中展示首页入口布局 |
| device-photo-main.png | 1280x720 | 1.78 | 实物照片展示区主图 | Photography | Placeholder | 如补充实拍照片则替换当前占位框，突出设备实物、佩戴或桌面摆放形态 |
| device-structure-view.png | 1280x720 | 1.78 | 结构视图展示区主图 | Diagram | Placeholder | 如补充结构图则替换当前占位框，突出手表硬件组成、屏幕、麦克风、联网与交互关系 |

**Status descriptions**:

- **Pending** - Needs AI generation, provide detailed description
- **Existing** - User already has image, place in `images/`
- **Placeholder** - Not yet processed, use dashed border placeholder in SVG

---

## IX. Content Outline

### Part 1: SRP Final Defense

#### Slide 01 - Cover

- **Layout**: Cover template with centered title and formal answer information
- **Title**: 基于AI大模型的声纹感知助残智能手表
- **Subtitle**: 校级大学生创新创业训练计划项目（SRP）结题答辩
- **Info**: 答辩人、指导教师、学院、日期

#### Slide 02 - 项目背景与研究意义

- **Layout**: Left-right split (5:5)
- **Title**: 面向助残场景的环境感知需求正在从“提醒”走向“实时理解”
- **Content**:
  - 左栏：视障/听障用户在日常出行和交流中面临的关键问题
  - 右栏：本项目的研究意义，突出可穿戴、实时性、低门槛交互
  - 底部：一句话总结“为什么要做这件事”

#### Slide 03 - 项目目标与总体方案

- **Layout**: Top-bottom split
- **Title**: 项目以“可运行原型 + 核心能力闭环”为阶段目标推进
- **Content**:
  - 上部：立项目标与阶段性取舍
  - 下部：总体方案卡片，展示 ESP32-S3 手表平台、离线危险识别、联网 AI 扩展三条主线
  - 强调当前结题定位是“阶段性原型验证”

#### Slide 04 - 系统架构与关键模块

- **Layout**: Left-right split (5:5)
- **Title**: 系统已形成显示交互、联网服务、危险检测与 AI 对话的模块化架构
- **Content**:
  - 左侧：系统架构模块框图
  - 右侧：四个关键模块说明
  - 模块包含：显示/触摸、联网、危险检测、AI 对话
  - 底部：说明运行模型是“前台可交互，后台持续联网与服务就绪探测”

#### Slide 05 - 离线模型识别危险信号

- **Layout**: Left-right split (5:5)
- **Title**: 离线危险声音识别是本次结题的核心技术亮点
- **Chart**: process_flow
- **Content**:
  - 左侧：离线识别流程图，链路为音频采集 -> 通道抽取 -> 重采样 -> 滑窗推理 -> 后处理 -> 告警输出
  - 右侧：识别对象、离线价值、当前验证结果
  - 突出：喇叭、警笛、背景音三类结果；识别后能触发红屏与提示音

#### Slide 06 - 系统实现与关键模块联动

- **Layout**: Top-bottom split
- **Title**: 从启动到底层服务联动，系统已打通“界面-识别-联网-提示”闭环
- **Content**:
  - 上部：启动与联动时序图
  - 下部：说明 `hardware_init`、`lvgl_task`、`network_service`、`danger_detection_service`、`official_chat_service` 的职责与配合
  - 强调 UI 单线程刷新、后台任务运行与告警联动

#### Slide 07 - 实物照片与界面展示

- **Layout**: Two-column evidence page
- **Title**: 当前原型已具备实物载体与页面级可视化交互表现
- **Content**:
  - 左侧：实物照片 / 结构视图占位区，若未补图则显示占位框
  - 右侧：竖版首页截图与 AI 对话截图
  - 底部：用简洁说明对应“主界面入口”“AI 页面状态与消息展示”

#### Slide 08 - 结题成果与创新点

- **Layout**: Three-column cards
- **Title**: 项目已完成阶段性成果，并形成助残场景下的嵌入式工程创新点
- **Content**:
  - 成果：可运行原型、离线危险声音识别、联网 AI 扩展底座
  - 创新：端侧 AI、助残导向设计、模块化系统集成
  - 价值：为下一阶段产品化验证提供稳定底座

#### Slide 09 - 存在问题与后续展望

- **Layout**: Left-right split (5:5)
- **Title**: 当前已完成核心验证，但仍需围绕传感器、稳定性与用户研究继续完善
- **Content**:
  - 左栏：当前不足，包含心率、BLE/手机协同、更多声音类别、长时间稳定性
  - 右栏：后续计划，包含扩展数据集、完善联网与功耗、推进用户测试
  - 页尾：答辩收束语，强调“已具备继续深化的工程基础”

---

## X. Speaker Notes Requirements

Generate corresponding speaker note files for each page, saved to the `notes/` directory:

- **File naming**: Match SVG names, e.g., `01_封面.md`
- **Content includes**: 5 分钟答辩讲稿、过渡语、时间控制提示
- **Presentation duration**: 5 分钟
- **Notes style**: 正式、精炼、适合校内学术答辩
- **Presentation purpose**: 汇报项目阶段成果并证明结题可成立

---

## XI. Technical Constraints Reminder

### SVG Generation Must Follow:

1. viewBox: `0 0 1280 720`
2. Background uses `<rect>` elements
3. Text wrapping uses `<tspan>` (`<foreignObject>` FORBIDDEN)
4. Transparency uses `fill-opacity` / `stroke-opacity`; `rgba()` FORBIDDEN
5. FORBIDDEN: `clipPath`, `mask`, `<style>`, `class`, `foreignObject`
6. FORBIDDEN: `textPath`, `animate*`, `script`, `marker`/`marker-end`
7. Arrows use `<polygon>` triangles instead of `<marker>`

### PPT Compatibility Rules:

- `<g opacity="...">` FORBIDDEN
- Image transparency uses overlay mask layers
- Inline styles only; external CSS and `@font-face` FORBIDDEN

---

## XII. Design Checklist

### Pre-generation

- [x] Content fits page capacity
- [x] Layout mode selected correctly
- [x] Colors used semantically

### Post-generation

- [ ] viewBox = `0 0 1280 720`
- [ ] No `<foreignObject>` elements
- [ ] All text readable (>=14px)
- [ ] Content within safe area
- [ ] Same elements maintain consistent style
- [ ] Colors conform to spec

---

## XIII. Next Steps

1. ✅ Design spec complete
2. **Next step**: Invoke Executor to generate SVGs
3. After SVG generation, generate `notes/total.md`, run post-processing, then export PPTX
