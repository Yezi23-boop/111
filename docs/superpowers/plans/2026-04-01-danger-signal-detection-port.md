# Danger Signal Detection Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在当前项目中移植 `idf-EDGE_lmpulse` 的危险音频识别能力，绑定主页 `option_6` 入口，进入页面后持续监听麦克风，识别到危险信号时触发页面与音频提醒，返回主页时停止识别并释放资源。

**Architecture:** 保留 `components/traffic_inference` 作为独立推理组件，在 `main` 层新增 `danger_detection_service` 与 `app_alert_manager` 风格的提醒链路，并用一个手写的自定义危险识别页面替代修改 GUI Guider 生成页面。所有 LVGL 对象操作都留在现有 `lvgl_task` 线程里处理，后台识别任务只更新状态和抛告警，避免再起第二套 LVGL 初始化。

**Tech Stack:** ESP-IDF 5.5.3, ESP32-S3, LVGL 9.3, FreeRTOS, C/C++, Edge Impulse exported library, Python `unittest` source-contract tests, `uv`

---

## File Map

### Create

- `D:\esp32S3\111\components\traffic_inference\**`
  - 从 `C:\Users\ye\Desktop\idf-EDGE_lmpulse\components\traffic_inference\**` 复制并裁剪为当前工程可编译版本。
- `D:\esp32S3\111\main\danger_detection_service.h`
  - 危险识别服务对外 API、状态与快照结构。
- `D:\esp32S3\111\main\danger_detection_service.c`
  - 识别任务生命周期、回调注册、状态缓存、启动/停止逻辑。
- `D:\esp32S3\111\main\app_alert_manager.h`
  - 告警请求结构和 UI/音频提醒统一入口。
- `D:\esp32S3\111\main\app_alert_manager.c`
  - 告警去重、危险覆盖提示和一次性提示音触发。
- `D:\esp32S3\111\main\audio_alert_player.h`
  - 提示音播放器接口。
- `D:\esp32S3\111\main\audio_alert_player.c`
  - 通过 `audio_codec_write()` 播放一次性告警 PCM。
- `D:\esp32S3\111\main\display_alert_adapter.h`
  - 危险覆盖提示适配器接口。
- `D:\esp32S3\111\main\display_alert_adapter.c`
  - 顶层覆盖提示状态缓存与 UI 线程应用逻辑。
- `D:\esp32S3\111\main\assets\tishiyinpin_pcm.h`
  - 从外部工程复制的告警 PCM 资源头文件。
- `D:\esp32S3\111\main\ui\custom\danger_detection_view.h`
  - 危险识别页面控件结构与操作接口。
- `D:\esp32S3\111\main\ui\custom\danger_detection_view.c`
  - 页面创建、状态文本更新、返回按钮绑定。
- `D:\esp32S3\111\main\ui\custom\danger_detection_controller.h`
  - 页面控制器接口。
- `D:\esp32S3\111\main\ui\custom\danger_detection_controller.c`
  - 页面打开/关闭、服务联动、UI 定时刷新。
- `D:\esp32S3\111\tests\test_traffic_inference_component_source.py`
  - 检查 `traffic_inference` 组件已接入且 CMake 依赖正确。
- `D:\esp32S3\111\tests\test_danger_detection_service_source.py`
  - 检查服务状态机、启动停止 API 和提醒链路接线。
- `D:\esp32S3\111\tests\test_danger_detection_ui_source.py`
  - 检查 `option_6` 绑定、自定义控制器初始化和 UI 刷新接线。

### Modify

- `D:\esp32S3\111\main\CMakeLists.txt`
  - 把 `traffic_inference` 加入 `REQUIRES`。
- `D:\esp32S3\111\main\lvgl_task.c`
  - 初始化危险识别控制器，并在 LVGL 主循环里处理危险覆盖 UI 同步。
- `D:\esp32S3\111\main\ui\generated\events_init.c`
  - 给 `screen_main_option_6` / `screen_main_Microphone` 绑定打开危险识别页事件。
- `D:\esp32S3\111\docs\context\knowledge\project\*.md`
  - 记录危险识别移植、提醒链路和页面生命周期结论。
- `D:\esp32S3\111\docs\context\CHANGELOG.md`
  - 增加本轮上下文变更记录。

### Reuse Without Modification

- `D:\esp32S3\111\components\audio_codec\**`
  - 继续作为录音/放音底层。
- `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`
  - 作为自定义控制器模式参考，不直接修改。
- `D:\esp32S3\111\main\ui\generated\gui_guider.h`
  - 保持生成头文件不变，危险识别页走手写页面，不新增 GUI Guider screen 字段。

---

### Task 1: 先把移植边界锁住的源码契约测试补齐

**Files:**
- Create: `D:\esp32S3\111\tests\test_traffic_inference_component_source.py`
- Create: `D:\esp32S3\111\tests\test_danger_detection_service_source.py`
- Create: `D:\esp32S3\111\tests\test_danger_detection_ui_source.py`

- [ ] **Step 1: 写 `traffic_inference` 组件接入失败测试**

```python
import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


class TrafficInferenceComponentSourceTests(unittest.TestCase):
    def test_component_cmake_and_sources_are_present(self) -> None:
        cmake_path = REPO_ROOT / "components" / "traffic_inference" / "CMakeLists.txt"
        self.assertTrue(cmake_path.exists(), "traffic_inference CMakeLists.txt should exist")

        cmake_source = cmake_path.read_text(encoding="utf-8")
        self.assertIn('traffic_audio_runtime.cc', cmake_source)
        self.assertIn('traffic_inference_postprocess.cc', cmake_source)
        self.assertIn('audio_codec', cmake_source)
        self.assertIn('espressif__esp-dsp', cmake_source)

        header_path = REPO_ROOT / "components" / "traffic_inference" / "include" / "traffic_audio_runtime.h"
        self.assertTrue(header_path.exists(), "traffic_audio_runtime.h should exist")
```

- [ ] **Step 2: 运行测试确认失败**

Run: `uv run python -m unittest tests.test_traffic_inference_component_source -v`
Expected: FAIL，提示 `traffic_inference CMakeLists.txt should exist`

- [ ] **Step 3: 写服务与提醒链路失败测试**

```python
import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN_DIR = REPO_ROOT / "main"


class DangerDetectionServiceSourceTests(unittest.TestCase):
    def test_service_and_alert_pipeline_are_wired(self) -> None:
        service_header = (MAIN_DIR / "danger_detection_service.h").read_text(encoding="utf-8")
        service_source = (MAIN_DIR / "danger_detection_service.c").read_text(encoding="utf-8")
        alert_source = (MAIN_DIR / "app_alert_manager.c").read_text(encoding="utf-8")

        self.assertIn("danger_detection_service_start", service_header)
        self.assertIn("danger_detection_service_stop", service_header)
        self.assertIn("DANGER_DETECTION_STATE_RUNNING", service_header)
        self.assertIn("traffic_inference_postprocess_set_alert_callback", service_source)
        self.assertIn("app_alert_manager_raise", service_source)
        self.assertIn("app_alert_manager_clear", service_source)
        self.assertIn("audio_alert_player_play_warning_once", alert_source)
```

- [ ] **Step 4: 运行测试确认失败**

Run: `uv run python -m unittest tests.test_danger_detection_service_source -v`
Expected: FAIL，提示 `danger_detection_service.h` 不存在

- [ ] **Step 5: 写 UI 接入失败测试**

```python
import pathlib
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


class DangerDetectionUiSourceTests(unittest.TestCase):
    def test_option6_and_lvgl_task_are_wired_to_danger_detection(self) -> None:
        events_source = (REPO_ROOT / "main" / "ui" / "generated" / "events_init.c").read_text(encoding="utf-8")
        lvgl_task_source = (REPO_ROOT / "main" / "lvgl_task.c").read_text(encoding="utf-8")

        self.assertIn("screen_main_option_6_event_handler", events_source)
        self.assertIn("danger_detection_ui_open()", events_source)
        self.assertIn("danger_detection_controller_init(&guider_ui);", lvgl_task_source)
        self.assertIn("display_alert_adapter_process_ui();", lvgl_task_source)
```

- [ ] **Step 6: 运行测试确认失败**

Run: `uv run python -m unittest tests.test_danger_detection_ui_source -v`
Expected: FAIL，提示 `screen_main_option_6_event_handler` 不存在

- [ ] **Step 7: 提交**

```bash
git add tests/test_traffic_inference_component_source.py tests/test_danger_detection_service_source.py tests/test_danger_detection_ui_source.py
git commit -m "测试: 增加危险信号识别移植的源码契约"
```

### Task 2: 迁入 `traffic_inference` 组件并把主工程依赖接上

**Files:**
- Create: `D:\esp32S3\111\components\traffic_inference\**`
- Modify: `D:\esp32S3\111\main\CMakeLists.txt`
- Test: `D:\esp32S3\111\tests\test_traffic_inference_component_source.py`

- [ ] **Step 1: 复制外部组件作为初始基线**

```powershell
Copy-Item -LiteralPath 'C:\Users\ye\Desktop\idf-EDGE_lmpulse\components\traffic_inference' `
  -Destination 'D:\esp32S3\111\components' `
  -Recurse -Force
```

- [ ] **Step 2: 在主组件依赖里加入 `traffic_inference`**

在 `D:\esp32S3\111\main\CMakeLists.txt` 的 `REQUIRES` 中加入：

```cmake
        traffic_inference
```

放在：

```cmake
        audio_codec
        mp3_player
        traffic_inference
        wifi_provision
```

- [ ] **Step 3: 运行组件测试确认通过**

Run: `uv run python -m unittest tests.test_traffic_inference_component_source -v`
Expected: PASS

- [ ] **Step 4: 做一次最小编译验证，先暴露组件级错误**

Run:

```powershell
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py build
```

Expected: 允许因为后续 `main` 侧符号缺失而失败，但不能再报 `traffic_inference` 组件目录缺失或 `REQUIRES traffic_inference` 未找到。

- [ ] **Step 5: 提交**

```bash
git add components/traffic_inference main/CMakeLists.txt
git commit -m "移植: 接入 traffic_inference 组件基线"
```

### Task 3: 实现危险识别服务与统一提醒链路

**Files:**
- Create: `D:\esp32S3\111\main\danger_detection_service.h`
- Create: `D:\esp32S3\111\main\danger_detection_service.c`
- Create: `D:\esp32S3\111\main\app_alert_manager.h`
- Create: `D:\esp32S3\111\main\app_alert_manager.c`
- Create: `D:\esp32S3\111\main\audio_alert_player.h`
- Create: `D:\esp32S3\111\main\audio_alert_player.c`
- Create: `D:\esp32S3\111\main\display_alert_adapter.h`
- Create: `D:\esp32S3\111\main\display_alert_adapter.c`
- Create: `D:\esp32S3\111\main\assets\tishiyinpin_pcm.h`
- Test: `D:\esp32S3\111\tests\test_danger_detection_service_source.py`

- [ ] **Step 1: 写服务头文件与状态枚举的最小实现**

```c
#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    DANGER_DETECTION_STATE_IDLE = 0,
    DANGER_DETECTION_STATE_STARTING,
    DANGER_DETECTION_STATE_RUNNING,
    DANGER_DETECTION_STATE_STOPPING,
    DANGER_DETECTION_STATE_ERROR,
} danger_detection_state_t;

typedef enum {
    DANGER_DETECTION_LABEL_NONE = 0,
    DANGER_DETECTION_LABEL_HORN,
    DANGER_DETECTION_LABEL_SIREN,
} danger_detection_label_t;

typedef struct {
    danger_detection_state_t state;
    danger_detection_label_t stable_label;
    esp_err_t last_error;
    bool danger_overlay_active;
} danger_detection_snapshot_t;

esp_err_t danger_detection_service_init(void);
esp_err_t danger_detection_service_start(void);
esp_err_t danger_detection_service_stop(uint32_t timeout_ms);
danger_detection_snapshot_t danger_detection_service_get_snapshot(void);
```

- [ ] **Step 2: 运行服务测试确认仍然失败，但失败点推进到 `.c` 缺失**

Run: `uv run python -m unittest tests.test_danger_detection_service_source -v`
Expected: FAIL，提示 `danger_detection_service.c` 不存在或缺少提醒链路接线

- [ ] **Step 3: 从外部工程复制 PCM 提示音资源**

```powershell
New-Item -ItemType Directory -Force -Path 'D:\esp32S3\111\main\assets' | Out-Null
Copy-Item -LiteralPath 'C:\Users\ye\Desktop\idf-EDGE_lmpulse\main\assets\tishiyinpin_pcm.h' `
  -Destination 'D:\esp32S3\111\main\assets\tishiyinpin_pcm.h' `
  -Force
```

- [ ] **Step 4: 实现一次性提示音播放器**

`D:\esp32S3\111\main\audio_alert_player.c` 最小实现用外部工程同一路线，但保持“不重新初始化 codec，只调用写接口”：

```c
esp_err_t audio_alert_player_play_warning_once(void)
{
    if (!s_player_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_player_state.playing) {
        return ESP_OK;
    }
    s_player_state.playing = true;
    BaseType_t task_created = xTaskCreate(audio_alert_player_task,
                                          "audio_alert",
                                          4096,
                                          NULL,
                                          4,
                                          &s_player_state.task_handle);
    if (task_created != pdPASS) {
        s_player_state.playing = false;
        s_player_state.task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
```

- [ ] **Step 5: 实现提醒管理层**

`D:\esp32S3\111\main\app_alert_manager.c` 里保留“进入危险态时只播放一次”的行为：

```c
esp_err_t app_alert_manager_raise(const app_alert_request_t *request)
{
    if (request == NULL || request->source == APP_ALERT_SOURCE_NONE) {
        return ESP_ERR_INVALID_ARG;
    }

    const bool same_source_active =
        s_alert_manager_state.active &&
        s_alert_manager_state.active_request.source == request->source;

    s_alert_manager_state.active_request = *request;

    if (!same_source_active) {
        ESP_ERROR_CHECK(display_alert_adapter_show_danger_overlay());
        s_alert_manager_state.active = true;
        (void)audio_alert_player_play_warning_once();
    }

    return ESP_OK;
}
```

- [ ] **Step 6: 用当前仓库的 LVGL 线程模型重写显示适配层**

不要照搬外部工程里会再次调用 `lv_port_init_small()` 的版本。`D:\esp32S3\111\main\display_alert_adapter.c` 改成“缓存命令，在 UI 线程中处理”：

```c
static volatile bool s_pending_show = false;
static volatile bool s_pending_hide = false;

esp_err_t display_alert_adapter_show_danger_overlay(void)
{
    s_pending_show = true;
    s_pending_hide = false;
    return ESP_OK;
}

esp_err_t display_alert_adapter_hide_danger_overlay(void)
{
    s_pending_hide = true;
    s_pending_show = false;
    return ESP_OK;
}

void display_alert_adapter_process_ui(void)
{
    if (s_pending_show) {
        s_pending_show = false;
        display_alert_apply_show();
    } else if (s_pending_hide) {
        s_pending_hide = false;
        display_alert_apply_hide();
    }
}
```

同时在头文件中声明：

```c
void display_alert_adapter_process_ui(void);
```

- [ ] **Step 7: 实现危险识别服务，把推理告警接到提醒管理层**

`D:\esp32S3\111\main\danger_detection_service.c` 的关键接线：

```c
static void danger_detection_on_alert(
    const traffic_inference_postprocess_alert_t *alert,
    void *user_data)
{
    (void)user_data;
    if (alert == NULL) {
        return;
    }

    if (alert->action == TRAFFIC_INFERENCE_POSTPROCESS_ALERT_ACTION_RAISE) {
        app_alert_request_t request = {
            .source = APP_ALERT_SOURCE_TRAFFIC_AUDIO,
            .severity = APP_ALERT_SEVERITY_DANGER,
            .label = alert->label == TRAFFIC_INFERENCE_POSTPROCESS_STABLE_LABEL_HORN
                         ? APP_ALERT_LABEL_HORN
                         : APP_ALERT_LABEL_SIREN,
        };
        (void)app_alert_manager_raise(&request);
    } else if (alert->action == TRAFFIC_INFERENCE_POSTPROCESS_ALERT_ACTION_CLEAR) {
        (void)app_alert_manager_clear(APP_ALERT_SOURCE_TRAFFIC_AUDIO);
    }
}
```

后台任务启动用：

```c
traffic_audio_runtime_config_t config = {
    .input_chunk_frames = 0U,
    .read_timeout_ms = 250U,
    .task_stack_size = 8192U,
    .task_priority = 5U,
};
```

启动时顺序：

```c
(void)app_alert_manager_init();
ESP_ERROR_CHECK(traffic_inference_postprocess_set_alert_callback(
    danger_detection_on_alert, NULL));
ESP_ERROR_CHECK(traffic_audio_runtime_start(&config));
```

停止时顺序：

```c
(void)app_alert_manager_clear(APP_ALERT_SOURCE_TRAFFIC_AUDIO);
(void)traffic_audio_runtime_stop(timeout_ms);
(void)traffic_inference_postprocess_set_alert_callback(NULL, NULL);
```

- [ ] **Step 8: 运行服务测试确认通过**

Run: `uv run python -m unittest tests.test_danger_detection_service_source -v`
Expected: PASS

- [ ] **Step 9: 提交**

```bash
git add main/danger_detection_service.h main/danger_detection_service.c main/app_alert_manager.h main/app_alert_manager.c main/audio_alert_player.h main/audio_alert_player.c main/display_alert_adapter.h main/display_alert_adapter.c main/assets/tishiyinpin_pcm.h
git commit -m "功能: 增加危险识别服务与提醒链路"
```

### Task 4: 做自定义危险识别页面与控制器，不改 GUI Guider 结构体

**Files:**
- Create: `D:\esp32S3\111\main\ui\custom\danger_detection_view.h`
- Create: `D:\esp32S3\111\main\ui\custom\danger_detection_view.c`
- Create: `D:\esp32S3\111\main\ui\custom\danger_detection_controller.h`
- Create: `D:\esp32S3\111\main\ui\custom\danger_detection_controller.c`
- Test: `D:\esp32S3\111\tests\test_danger_detection_ui_source.py`

- [ ] **Step 1: 写一个最小危险识别页面视图**

`D:\esp32S3\111\main\ui\custom\danger_detection_view.h`：

```c
typedef struct danger_detection_view danger_detection_view_t;

typedef struct {
    const char *title_text;
    void (*back_action_cb)(void *user_data);
    void *user_data;
} danger_detection_view_config_t;

danger_detection_view_t *danger_detection_view_create(
    const danger_detection_view_config_t *config);
void danger_detection_view_destroy(danger_detection_view_t *view);
lv_obj_t *danger_detection_view_get_screen(danger_detection_view_t *view);
void danger_detection_view_set_status(danger_detection_view_t *view,
                                      const char *title,
                                      const char *detail);
```

- [ ] **Step 2: 在视图实现里把页面控件锁到单一职责**

核心结构：

```c
struct danger_detection_view {
    lv_obj_t *screen;
    lv_obj_t *title_label;
    lv_obj_t *detail_label;
    lv_obj_t *back_btn;
};
```

最小状态更新函数：

```c
void danger_detection_view_set_status(danger_detection_view_t *view,
                                      const char *title,
                                      const char *detail)
{
    if (view == NULL) {
        return;
    }
    lv_label_set_text(view->title_label, title != NULL ? title : "");
    lv_label_set_text(view->detail_label, detail != NULL ? detail : "");
}
```

- [ ] **Step 3: 写控制器头文件**

```c
void danger_detection_controller_init(lv_ui *ui);
void danger_detection_ui_open(void);
void danger_detection_controller_poll_ui(void);
```

- [ ] **Step 4: 实现控制器生命周期，模式对齐 `ai_ui_controller.c`**

关键逻辑：

```c
static void danger_detection_back_event(void *user_data)
{
    (void)user_data;
    (void)danger_detection_service_stop(2000U);
    lv_screen_load_anim(s_ui->screen_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, true);
    danger_detection_destroy_screen();
}

void danger_detection_ui_open(void)
{
    danger_detection_ensure_screen_created();
    if (s_view == NULL) {
        return;
    }

    (void)danger_detection_service_start();
    danger_detection_refresh_status();
    lv_screen_load_anim(danger_detection_view_get_screen(s_view),
                        LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}
```

- [ ] **Step 5: 在控制器轮询里把服务快照映射到页面文案**

```c
void danger_detection_controller_poll_ui(void)
{
    if (s_view == NULL || lv_screen_active() != danger_detection_view_get_screen(s_view)) {
        return;
    }

    danger_detection_snapshot_t snapshot = danger_detection_service_get_snapshot();
    if (snapshot.state == DANGER_DETECTION_STATE_STARTING) {
        danger_detection_view_set_status(s_view, "初始化中", "正在启动危险信号识别");
    } else if (snapshot.state == DANGER_DETECTION_STATE_RUNNING &&
               snapshot.stable_label == DANGER_DETECTION_LABEL_HORN) {
        danger_detection_view_set_status(s_view, "检测到喇叭", "请注意周边危险信号");
    } else if (snapshot.state == DANGER_DETECTION_STATE_RUNNING &&
               snapshot.stable_label == DANGER_DETECTION_LABEL_SIREN) {
        danger_detection_view_set_status(s_view, "检测到警笛", "请及时避让");
    } else if (snapshot.state == DANGER_DETECTION_STATE_RUNNING) {
        danger_detection_view_set_status(s_view, "正在监听", "持续监听麦克风中的危险信号");
    } else if (snapshot.state == DANGER_DETECTION_STATE_ERROR) {
        danger_detection_view_set_status(s_view, "启动失败", esp_err_to_name(snapshot.last_error));
    }
}
```

- [ ] **Step 6: 运行 UI 测试确认仍失败，但失败点推进到事件绑定**

Run: `uv run python -m unittest tests.test_danger_detection_ui_source -v`
Expected: FAIL，提示 `screen_main_option_6_event_handler` 不存在

- [ ] **Step 7: 提交**

```bash
git add main/ui/custom/danger_detection_view.h main/ui/custom/danger_detection_view.c main/ui/custom/danger_detection_controller.h main/ui/custom/danger_detection_controller.c
git commit -m "界面: 增加危险信号识别自定义页面"
```

### Task 5: 把 `option_6`、LVGL 任务和危险覆盖同步全部接上

**Files:**
- Modify: `D:\esp32S3\111\main\ui\generated\events_init.c`
- Modify: `D:\esp32S3\111\main\lvgl_task.c`
- Test: `D:\esp32S3\111\tests\test_danger_detection_ui_source.py`

- [ ] **Step 1: 在 `events_init.c` 里增加 `option_6` 事件**

```c
static void screen_main_option_6_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        danger_detection_ui_open();
        break;
    }
    default:
        break;
    }
}
```

注册到 `events_init_screen_main()`：

```c
    lv_obj_add_event_cb(ui->screen_main_option_6, screen_main_option_6_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_main_Microphone, screen_main_option_6_event_handler, LV_EVENT_ALL, ui);
```

- [ ] **Step 2: 给 `lvgl_task.c` 加上初始化与 UI 线程同步调用**

头文件区增加：

```c
#include "danger_detection_controller.h"
#include "display_alert_adapter.h"
```

初始化区增加：

```c
    danger_detection_controller_init(&guider_ui);
```

主循环里在 `lv_timer_handler()` 前增加：

```c
        display_alert_adapter_process_ui();
        danger_detection_controller_poll_ui();
```

- [ ] **Step 3: 运行 UI 测试确认通过**

Run: `uv run python -m unittest tests.test_danger_detection_ui_source -v`
Expected: PASS

- [ ] **Step 4: 跑全量源码契约测试**

Run:

```bash
uv run python -m unittest \
  tests.test_traffic_inference_component_source \
  tests.test_danger_detection_service_source \
  tests.test_danger_detection_ui_source -v
```

Expected: PASS

- [ ] **Step 5: 提交**

```bash
git add main/ui/generated/events_init.c main/lvgl_task.c
git commit -m "接线: 绑定 option_6 与危险识别页面生命周期"
```

### Task 6: 完整构建验证、板端验证和上下文回写

**Files:**
- Modify: `D:\esp32S3\111\docs\context\knowledge\project\danger-signal-detection-port.md`
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`

- [ ] **Step 1: 做完整构建验证**

Run:

```powershell
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py build
```

Expected: BUILD SUCCESSFUL

- [ ] **Step 2: 如果构建失败，先修构建再重新执行三组源码契约测试**

Run:

```bash
uv run python -m unittest tests.test_traffic_inference_component_source -v
uv run python -m unittest tests.test_danger_detection_service_source -v
uv run python -m unittest tests.test_danger_detection_ui_source -v
```

Expected: 全部 PASS

- [ ] **Step 3: 板端验证进入/退出页面的资源释放**

Run:

```powershell
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
$env:ESP_IDF_MONITOR_TEST='1'
idf.py flash monitor
```

Expected:
- 点击 `option_6` 后日志出现服务启动、`traffic_audio runtime started`
- 识别到危险音频时出现危险提示与一次性提示音
- 返回主页后日志出现服务停止、告警清理、`audio_codec` 释放

- [ ] **Step 4: 清理 monitor 相关进程并释放串口**

Run:

```powershell
Get-Process | Where-Object { $_.ProcessName -match 'idf_monitor|python' -and $_.Path -match 'idf' } | Stop-Process
Remove-Item Env:ESP_IDF_MONITOR_TEST -ErrorAction SilentlyContinue
```

Expected: 串口被释放，没有残留 monitor 进程

- [ ] **Step 5: 回写上下文知识**

在 `D:\esp32S3\111\docs\context\knowledge\project\danger-signal-detection-port.md` 记录：

```md
---
id: danger-signal-detection-port
tags: project, audio, traffic-inference, lvgl, alert, esp32-s3
summary: 记录危险音频识别移植、option_6 页面生命周期、统一提醒链路和资源释放边界。
last_reviewed: 2026-04-01
---
```

正文至少覆盖：
- `option_6` 为入口
- 危险识别页进入/退出生命周期
- `traffic_inference -> danger_detection_service -> app_alert_manager` 链路
- 提示音与录音链路共存边界

- [ ] **Step 6: 更新上下文变更日志**

在 `D:\esp32S3\111\docs\context\CHANGELOG.md` 增加一行：

```md
- 2026-04-01: 新增危险信号识别移植与提醒链路上下文。
```

- [ ] **Step 7: 提交**

```bash
git add docs/context/knowledge/project/danger-signal-detection-port.md docs/context/CHANGELOG.md
git commit -m "文档: 回写危险信号识别移植上下文"
```

---

## Self-Review

### Spec Coverage

- `option_6` 作为入口：Task 5
- 危险识别独立页面：Task 4
- 进入页面自动启动识别：Task 4 + Task 5
- 离开页面停止并释放资源：Task 3 + Task 4
- 保留 UI 覆盖提示与一次性提示音：Task 3
- 保持单 LVGL 线程，不新增第二套 `lv_port_init_small()`：Task 3 + Task 5
- 构建、板端、上下文回写：Task 6

### Placeholder Scan

- 无 `TODO` / `TBD`
- 每个任务都给出了具体文件、代码或命令
- 关键路径没有“类似前文”式省略

### Type Consistency

- 服务状态统一使用 `danger_detection_state_t`
- 页面控制器统一使用 `danger_detection_controller_*`
- 页面视图统一使用 `danger_detection_view_*`
- 提醒管理统一使用 `app_alert_manager_*`

