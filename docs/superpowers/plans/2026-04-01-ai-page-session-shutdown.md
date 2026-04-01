# AI Page Session Shutdown Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让正式 AI 页和实验页在返回主页或离开页面时彻底释放 `official_chat` 会话、音频链和消息缓存，使 AI 对话只在当前页面内运行。

**Architecture:** 保留现有 `official_chat_service` 后台任务骨架，但新增显式 `shutdown` 接口负责销毁 `official_chat` 句柄并清空缓存。UI 层在返回主页时调用该接口，同时删除当前 AI 页面对象；下次重新进入时再按全新会话创建。

**Tech Stack:** ESP-IDF 5.5.3, LVGL 9.3, C, `official_chat`, hand-written AI view

---

### Task 1: 给 `official_chat_service` 增加会话销毁接口

**Files:**
- Modify: `D:\esp32S3\111\main\official_chat_service.h`
- Modify: `D:\esp32S3\111\main\official_chat_service.c`
- Test: `D:\esp32S3\111\tests\test_official_chat_service_source.py`

- [ ] **Step 1: 先写源码级失败测试**

在 `D:\esp32S3\111\tests\test_official_chat_service_source.py` 增加断言，检查：
- 头文件公开 `official_chat_service_shutdown`
- 源文件实现中包含：
  - `official_chat_destroy(s_chat_handle)`
  - `s_chat_handle = NULL`
  - `s_message_count = 0`
  - `s_service_state = OFFICIAL_CHAT_SERVICE_STATE_STOPPED`

- [ ] **Step 2: 运行测试确认当前失败**

运行：
`uv run python -m unittest tests.test_official_chat_service_source -v`

预期：
- 新增断言失败，因为当前还没有 `official_chat_service_shutdown`

- [ ] **Step 3: 最小实现 shutdown 接口**

在 `D:\esp32S3\111\main\official_chat_service.h` 增加声明：

```c
void official_chat_service_shutdown(void);
```

在 `D:\esp32S3\111\main\official_chat_service.c` 增加一个内部清理函数和公开 shutdown：

```c
static void official_chat_service_clear_cached_messages_locked(void) {
    memset(s_last_user_text, 0, sizeof(s_last_user_text));
    memset(s_last_assistant_text, 0, sizeof(s_last_assistant_text));
    memset(s_message_history, 0, sizeof(s_message_history));
    s_message_count = 0;
}

void official_chat_service_shutdown(void) {
    official_chat_handle_t handle_to_destroy = NULL;

    official_chat_service_lock();
    s_foreground_requested = false;
    if (s_chat_handle != NULL) {
        handle_to_destroy = s_chat_handle;
        s_chat_handle = NULL;
    }
    official_chat_service_clear_cached_messages_locked();
    s_last_error = ESP_OK;
    s_service_state = OFFICIAL_CHAT_SERVICE_STATE_STOPPED;
    official_chat_service_unlock();

    if (handle_to_destroy != NULL) {
        official_chat_destroy(handle_to_destroy);
    }
}
```

- [ ] **Step 4: 运行测试确认通过**

运行：
`uv run python -m unittest tests.test_official_chat_service_source -v`

预期：
- PASS

- [ ] **Step 5: 提交本任务**

```bash
git add D:\esp32S3\111\main\official_chat_service.h D:\esp32S3\111\main\official_chat_service.c D:\esp32S3\111\tests\test_official_chat_service_source.py
git commit -m "功能：为AI会话增加显式销毁接口"
```

### Task 2: 正式 AI 页返回主页时释放页面与 AI 会话

**Files:**
- Modify: `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`
- Test: `D:\esp32S3\111\tests\test_ai_ui_entry_source.py`

- [ ] **Step 1: 先写源码级失败测试**

在 `D:\esp32S3\111\tests\test_ai_ui_entry_source.py` 增加断言：
- 返回主页路径调用 `official_chat_service_shutdown()`
- 返回主页路径会清空 `s_view`
- 再次打开时仍通过 `ai_ui_ensure_screen_created()`

- [ ] **Step 2: 运行测试确认当前失败**

运行：
`uv run python -m unittest tests.test_ai_ui_entry_source -v`

预期：
- 新增断言失败，因为当前返回主页还只调用 `official_chat_service_leave_foreground()`

- [ ] **Step 3: 最小修改返回主页路径**

在 `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c` 中：

1. 增加一个释放 view 的内部函数：

```c
static void ai_ui_destroy_screen(void) {
    if (s_view == NULL) {
        return;
    }

    lv_obj_t *screen = ai_chat_view_get_screen(s_view);
    s_view = NULL;

    if (screen != NULL && lv_obj_is_valid(screen)) {
        lv_obj_delete(screen);
    }
}
```

2. 修改返回主页回调：

```c
static void ai_ui_back_event(void *user_data) {
    (void)user_data;

    if (s_ui == NULL || s_ui->screen_main == NULL) {
        ESP_LOGW(TAG, "screen_main not ready when leaving ai page");
        return;
    }

    official_chat_service_shutdown();
    s_foreground_requested = false;
    ai_ui_destroy_screen();
    lv_screen_load_anim(s_ui->screen_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0,
                        false);
}
```

- [ ] **Step 4: 运行测试确认通过**

运行：
`uv run python -m unittest tests.test_ai_ui_entry_source -v`

预期：
- PASS

- [ ] **Step 5: 提交本任务**

```bash
git add D:\esp32S3\111\main\ui\custom\ai_ui_controller.c D:\esp32S3\111\tests\test_ai_ui_entry_source.py
git commit -m "功能：返回主页时销毁正式AI会话与页面"
```

### Task 3: 实验页同步离开即释放会话

**Files:**
- Modify: `D:\esp32S3\111\main\ai_experiment_ui.c`
- Test: `D:\esp32S3\111\tests\test_ai_experiment_ui_source.py`

- [ ] **Step 1: 先写源码级失败测试**

在 `D:\esp32S3\111\tests\test_ai_experiment_ui_source.py` 增加断言：
- 若实验页存在离开/关闭路径，则调用 `official_chat_service_shutdown()`

- [ ] **Step 2: 运行测试确认当前失败或确认现状**

运行：
`uv run python -m unittest tests.test_ai_experiment_ui_source -v`

预期：
- 若实验页已有退出路径，新断言失败
- 若没有退出路径，测试应明确断言“不存在退出按钮路径，本任务不强加新按钮”

- [ ] **Step 3: 按最小边界处理实验页**

若 `D:\esp32S3\111\main\ai_experiment_ui.c` 已有退出/返回主页路径，则将其退出处理同步改为：

```c
official_chat_service_shutdown();
```

若当前没有退出路径，则仅保持文件不改，并让测试文案记录“实验页当前无独立返回路径，不新增交互”。

- [ ] **Step 4: 运行测试确认通过**

运行：
`uv run python -m unittest tests.test_ai_experiment_ui_source -v`

预期：
- PASS

- [ ] **Step 5: 提交本任务**

```bash
git add D:\esp32S3\111\main\ai_experiment_ui.c D:\esp32S3\111\tests\test_ai_experiment_ui_source.py
git commit -m "功能：同步实验页AI会话退出语义"
```

### Task 4: 全量验证与上下文更新

**Files:**
- Modify: `D:\esp32S3\111\docs\context\knowledge\project\ai-ui-entry-network-guidance.md`
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`

- [ ] **Step 1: 运行相关源码测试**

运行：
`uv run python -m unittest tests.test_official_chat_service_source tests.test_ai_ui_entry_source tests.test_ai_experiment_ui_source -v`

预期：
- 全部 PASS

- [ ] **Step 2: 运行构建验证**

运行：
`cmd /c "call D:\esp-idf\v5.5.3\esp-idf\export.bat >nul && idf.py build"`

预期：
- 构建成功

- [ ] **Step 3: 更新上下文文档**

在 `D:\esp32S3\111\docs\context\knowledge\project\ai-ui-entry-network-guidance.md` 增补：
- 正式 AI 页返回主页时会调用 `official_chat_service_shutdown()`
- AI 会话、消息缓存和页面对象会被销毁
- 下次进入页面时重新创建

在 `D:\esp32S3\111\docs\context\CHANGELOG.md` 增加一行：
- `2026-04-01：将正式 AI 页返回主页语义从“退后台”改为“销毁 official_chat 会话与页面对象”，使 AI 对话只在当前页面内运行。`

- [ ] **Step 4: 重建上下文索引并检查**

运行：
`uv run python scripts/context/build_index.py`

运行：
`uv run python scripts/context/check.py`

预期：
- 错误 0

- [ ] **Step 5: 真机回归验证**

执行：
`cmd /c "call D:\esp-idf\v5.5.3\esp-idf\export.bat >nul && idf.py -p COM3 flash"`

真机步骤：
1. 进入 AI 页面
2. 看到 AI 进入对话态
3. 点击“返回主页”
4. 确认回到主菜单
5. 再次进入 AI 页面，确认消息已清空并重新启动 AI

- [ ] **Step 6: 提交本任务**

```bash
git add D:\esp32S3\111\docs\context\knowledge\project\ai-ui-entry-network-guidance.md D:\esp32S3\111\docs\context\CHANGELOG.md
git commit -m "文档：记录AI页面离开即销毁会话语义"
```
