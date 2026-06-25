---
description: "ESP-IDF 构建：export.ps1 + idf.py build/fullclean/app-flash"
---

# ESP-IDF Build

执行 ESP-IDF 构建流程。根据参数选择构建模式。

## 使用方式

`/idf-build [mode]`

mode 可选值：
- `build`（默认）：`idf.py build`
- `fullclean`：`idf.py fullclean && idf.py build`
- `flash`：`idf.py -p <PORT> app-flash`
- `monitor`：`idf.py -p <PORT> monitor`（限时 60 秒）

## 执行步骤

1. 检查 `export.ps1` 是否可用（路径：`D:\esp-idf\v5.5.3\esp-idf\export.ps1`）
2. 根据 mode 执行对应命令：
   - `build`：`powershell -Command "& 'D:\esp-idf\v5.5.3\esp-idf\export.ps1' 2>&1 | Select-Object -Last 5; idf.py build 2>&1 | Select-Object -Last 30"`
   - `fullclean`：先 `idf.py fullclean` 再 `idf.py build`
   - `flash`：`idf.py -p <PORT> app-flash`
   - `monitor`：`idf.py -p <PORT> monitor`（60 秒后自动退出）
3. 输出构建结果摘要（成功/失败 + 关键错误信息）

## 注意事项

- 修改过 `sdkconfig` 时必须先 `fullclean` 再 `build`
- 常规代码改动用 `app-flash`，不要用 `idf.py flash`（会写入多个分区）
- 构建失败时先检查是否需要 `fullclean`（ninja 缓存问题）
- 串口验证：`app-flash` 后再限时采集 `monitor`
