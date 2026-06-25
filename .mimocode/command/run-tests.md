---
description: "运行 source tests：pytest 测试套件"
---

# Source Tests

运行项目的 Python source tests 验证实现正确性。

## 使用方式

`/run-tests [pattern]`

- 无参数：运行所有 source tests
- `pattern`：指定测试文件模式（如 `memory_watch`、`ap_portal`）

## 执行步骤

1. 确定测试文件：
   - 默认：`tests/test_*_source.py`
   - 指定 pattern：`tests/test_*<pattern>*_source.py`
2. 运行 pytest：
   ```
   uv run python -m pytest <test_files> -q
   ```
3. 输出结果摘要：
   - 通过/失败数量
   - 失败用例详情（如有）

## 测试文件位置

- `tests/test_memory_watch_service_source.py`
- `tests/test_memory_watch_ui_source.py`
- `tests/test_memory_watch_voice_client_source.py`
- `tests/test_ap_portal_adapter_source.py`
- `tests/test_ap_portal_http_api_source.py`

## 注意事项

- 使用 `uv run python -m pytest` 而非直接 `pytest`
- `-q` 参数减少输出噪音
- 测试失败时查看具体断言信息，不要盲目重试
