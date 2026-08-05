#!/usr/bin/env bash
# ESP-IDF 构建统一入口（MSYS/Git Bash 环境下专用）。
#
# 为什么需要它：ESP-IDF 5.x 检测到 MSYSTEM/MINGW 环境变量就拒绝工作
# （idf_tools.py 打印 "MSys/Mingw is not supported" 后退出）。而 pi 等
# agent 环境的命令执行器是 Git Bash / MSYS2，其运行时 msys-2.0.dll 在
# spawn 任何 Windows 子进程时都会强制注入 MSYSTEM=MINGW64，bash 层
# unset 无效，因此无法直接调用 idf.py。
#
# 解法：借道 Windows 原生 pwsh 作为"环境净化器"——pwsh 用自己的环境块
# spawn 子进程，删除 MSYSTEM 后派生的 python/idf.py 不再携带该变量，
# 即可通过 ESP-IDF 的环境检测。此方案已在 C:\Users\ye\bin\idf.py 验证。
#
# 用法（所有参数原样透传给 idf.py）：
#   scripts/board/esp_idf_build.sh build
#   scripts/board/esp_idf_build.sh -p COM3 app-flash
#   scripts/board/esp_idf_build.sh -p COM3 flash monitor

set -euo pipefail

# 仓库根目录：脚本位于 <repo>/scripts/board/ 下
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# ---- 环境定位 ----
# IDF_PATH：优先使用已导出的环境变量，否则回退到本机默认安装
IDF_PATH="${IDF_PATH:-D:/esp-idf/v5.5.3/esp-idf}"
# IDF Python venv：优先 IDF_PYTHON_ENV_PATH，其次按 IDF_TOOLS_PATH 推导
if [ -n "${IDF_PYTHON_ENV_PATH:-}" ]; then
    VENV_PY_WIN="${IDF_PYTHON_ENV_PATH}/Scripts/python.exe"
elif [ -n "${IDF_TOOLS_PATH:-}" ]; then
    VENV_PY_WIN="${IDF_TOOLS_PATH}/python_env/idf5.5_py3.11_env/Scripts/python.exe"
else
    VENV_PY_WIN="D:/esp-idf/5.3/tools.espressif/python_env/idf5.5_py3.11_env/Scripts/python.exe"
fi

# bash 侧检查用 POSIX 路径（Windows 反斜杠在 bash 里会被当成转义符）
IDF_PATH_POSIX="$(cygpath -u "${IDF_PATH}" 2>/dev/null || echo "${IDF_PATH}")"
VENV_PY_POSIX="$(cygpath -u "${VENV_PY_WIN}" 2>/dev/null || echo "${VENV_PY_WIN}")"

# ---- 前置检查：失败点提前暴露，而不是让错误发生在 pwsh 深处 ----
if [ ! -f "${IDF_PATH_POSIX}/tools/idf.py" ]; then
    echo "[esp_idf_build] IDF_PATH 无效: ${IDF_PATH}" >&2
    exit 1
fi
if [ ! -f "${VENV_PY_POSIX}" ]; then
    echo "[esp_idf_build] IDF python venv 不存在: ${VENV_PY_WIN}" >&2
    echo "[esp_idf_build] 可通过环境变量 IDF_PYTHON_ENV_PATH 指定" >&2
    exit 1
fi
if ! command -v pwsh >/dev/null 2>&1; then
    echo "[esp_idf_build] 未找到 pwsh（PowerShell 7），需要它作为环境净化跳板" >&2
    exit 1
fi

# ---- 构建子进程环境 ----
# PATH 过滤掉 MSYS 工具链（mingw/msys/usr/bin），只留 Windows 原生程序，
# 避免 MSYS 的 find/rm 等与原生工具混用导致行为不一致。
export IDF_PATH_TARGET="$(cygpath -w "${IDF_PATH_POSIX}")"
export VENV_PY="$(cygpath -w "${VENV_PY_POSIX}")"
export WINPATH=$(echo "$PATH" | tr ':' '\n' | grep -viE 'mingw|msys|/usr/bin' | while read -r p; do cygpath -w "$p" 2>/dev/null; done | tr '\n' ';')
# 参数经 base64 传递，避免 bash→pwsh 之间引号/转义丢失
export ARGS_B64=$(printf '%s\n' "$@" | base64 -w0)

# ---- pwsh 跳板：净化环境并执行 idf.py ----
pwsh -NoProfile -Command '
$ErrorActionPreference = "Stop"
$env:PATH = $env:WINPATH
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:MSYSTEM_CHOST,Env:MSYSTEM_PREFIX,Env:MSYSTEM_CARCH,Env:MSYSTEM_ARCH -ErrorAction SilentlyContinue
Remove-Item Env:MINGW_PREFIX,Env:MINGW_CHOST -ErrorAction SilentlyContinue
$env:IDF_PATH = $env:IDF_PATH_TARGET

# 激活 ESP-IDF 环境（stdout 是 powershell 格式的 export 语句，stderr 是日志）
$act = & $env:VENV_PY "$env:IDF_PATH/tools/activate.py" --export --shell powershell 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Error "ESP-IDF 环境激活失败（activate.py 退出码 $LASTEXITCODE），请检查 IDF python 环境"
    exit $LASTEXITCODE
}
$act | Out-String | Invoke-Expression

# 解码参数，直接用 venv python 调用真正的 idf.py（绕过 idf-exe 启动器：
# 激活后 PATH 里的 idf.py 解析到 tools/idf-exe/*/idf.py.exe，其 --version 返回
# 自身版本且环境错位时静默无输出，无法据此判断构建结果）
$bytes = [Convert]::FromBase64String($env:ARGS_B64)
$text  = [System.Text.Encoding]::UTF8.GetString($bytes)
$idfArgs = @($text -split "`n" | Where-Object { $_ -ne "" })
if ($idfArgs.Count -eq 0) { & $env:VENV_PY "$env:IDF_PATH/tools/idf.py" }
else { & $env:VENV_PY "$env:IDF_PATH/tools/idf.py" @idfArgs }
exit $LASTEXITCODE
'
exit $?
