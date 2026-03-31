# MP3 Player Time Weather Decoupling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `D:\esp32S3\111\main\time_weather.c` 不再持有 `mp3_player` 初始化或演示播放逻辑，为后续 AI 对话和独立音乐应用接入先清掉错误耦合。

**Architecture:** 本次只做最小脱钩，不改 `components/mp3_player` 的接口和实现。通过一条源码级回归测试约束 `time_weather.c` 不再引用 `mp3_player`，再做最小生产代码修改，并更新上下文记录。

**Tech Stack:** ESP-IDF, C, Python `unittest`, PowerShell, docs/context 工作流

---

## File Structure

- Modify: `D:\esp32S3\111\main\time_weather.c`
  - 移除 `mp3_player` include、初始化和示例播放残留，让该文件只保留时间显示职责。
- Modify: `D:\esp32S3\111\tests\test_official_chat_experiment_source.py`
  - 不动。
- Create: `D:\esp32S3\111\tests\test_time_weather_source.py`
  - 增加源码级回归测试，断言 `time_weather.c` 不再引用 `mp3_player`。
- Modify: `D:\esp32S3\111\docs\context\knowledge\project\repo-overview.md`
  - 补充当前仓库里 `mp3_player` 已从时间天气任务脱钩的知识点。
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`
  - 增加本次脱钩记录。

### Task 1: Add Regression Test For Time Weather Decoupling

**Files:**
- Create: `D:\esp32S3\111\tests\test_time_weather_source.py`
- Test: `D:\esp32S3\111\tests\test_time_weather_source.py`

- [ ] **Step 1: Write the failing test**

```python
import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
TIME_WEATHER_SOURCE = REPO_ROOT / "main" / "time_weather.c"


class TimeWeatherSourceTests(unittest.TestCase):
    def test_time_weather_no_longer_references_mp3_player(self) -> None:
        source = TIME_WEATHER_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn('#include "mp3_player.h"', source)
        self.assertNotIn("mp3_player_init(", source)
        self.assertNotIn("mp3_player_play_file(", source)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.test_time_weather_source -v`
Expected: FAIL，因为当前 `time_weather.c` 仍包含 `#include "mp3_player.h"` 和 `mp3_player_init()`

- [ ] **Step 3: Create the test file**

Use `apply_patch` to add:

```python
import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
TIME_WEATHER_SOURCE = REPO_ROOT / "main" / "time_weather.c"


class TimeWeatherSourceTests(unittest.TestCase):
    def test_time_weather_no_longer_references_mp3_player(self) -> None:
        source = TIME_WEATHER_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn('#include "mp3_player.h"', source)
        self.assertNotIn("mp3_player_init(", source)
        self.assertNotIn("mp3_player_play_file(", source)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 4: Re-run the test to keep it red**

Run: `python -m unittest tests.test_time_weather_source -v`
Expected: FAIL，确认测试确实卡在旧耦合点，而不是测试脚本写错

### Task 2: Remove MP3 Player Coupling From Time Weather

**Files:**
- Modify: `D:\esp32S3\111\main\time_weather.c`
- Test: `D:\esp32S3\111\tests\test_time_weather_source.py`

- [ ] **Step 1: Remove `mp3_player` include and initialization**

Update `D:\esp32S3\111\main\time_weather.c` so that:

```c
#include "time_weather.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "get_time.h"
#include "clock_functions.h"

static const char *TAG = "audio_example";

void time_and_weather(void *pvParameters)
{
    esp_wait_sntp_sync(); // 初始SNTP同步,确保时间准确

    uint32_t time_update_counter = 0; // 时间更新计数器
    while (1)
    {
        if (time_update_counter % 120 == 0)
        {
            update_now_time();
            update_digital_clock(now_time.hour, now_time.min, now_time.sec);
        }
        time_update_counter++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

Notes:
- 删除 `#include "mp3_player.h"`
- 删除 `mp3_player_init()` 整段
- 删除示例 `mp3_player_play_file(...)` 注释
- 不要顺手改任务名、函数名或时间更新逻辑

- [ ] **Step 2: Run the focused test**

Run: `python -m unittest tests.test_time_weather_source -v`
Expected: PASS

- [ ] **Step 3: Run related regression test**

Run: `python -m unittest tests.test_official_chat_experiment_source -v`
Expected: PASS

- [ ] **Step 4: Build the firmware**

Run:

```powershell
if ($env:IDF_PATH) { . "$env:IDF_PATH\export.ps1"; idf.py build } else { Write-Error 'IDF_PATH is not set' }
```

Expected:
- build 成功
- 没有因为删除 `mp3_player` 引用而影响主工程链接

### Task 3: Update Context And Changelog

**Files:**
- Modify: `D:\esp32S3\111\docs\context\knowledge\project\repo-overview.md`
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`

- [ ] **Step 1: Update repo overview**

Add one short bullet to `D:\esp32S3\111\docs\context\knowledge\project\repo-overview.md` in the module/startup area, stating that:

```md
- `components/mp3_player` 当前保留为独立底层播放器组件，已不再由 `main/time_weather.c` 在启动时隐式初始化。
```

- [ ] **Step 2: Update changelog**

Append one line to `D:\esp32S3\111\docs\context\CHANGELOG.md`:

```md
- 2026-03-31：将 `mp3_player` 从 `main/time_weather.c` 中脱钩，移除时间天气任务中的播放器初始化与示例播放残留，并补充源码级回归测试。
```

- [ ] **Step 3: Refresh context index**

Run: `uv run python scripts/context/build_index.py`
Expected: 成功生成更新后的 `context/index/context-index.json`

- [ ] **Step 4: Run context quality check**

Run: `uv run python scripts/context/check.py`
Expected: `错误: 0`

## Self-Review

- Spec coverage:
  - 已覆盖 `time_weather.c` 去掉 `mp3_player` include / init / play 注释
  - 已覆盖新增源码级回归测试
  - 已覆盖构建验证与上下文更新
  - 没有扩展到音乐应用页、AI 抢占策略或 `mp3_player` 组件重构
- Placeholder scan:
  - 无 `TODO` / `TBD`
  - 所有命令、路径、测试目标都已具体给出
- Type consistency:
  - 测试文件路径、源码路径、函数名与现有仓库一致
  - 没有引入新的未定义接口
