"""main/CMakeLists.txt glob 化后的 source 契约检查助手。

背景：main/CMakeLists.txt 由手工逐行列出源文件改为 `file(GLOB ...)` 自动收集后，
「防漏编译」保险从「路径必须出现在 CMakeLists.txt」变为：
1) 源文件真实存在（防止文件被误删导致功能消失）；
2) 文件所在目录被 CMakeLists.txt 中的 glob 规则覆盖（新文件落入被 glob 的目录即自动编译）。

REQUIRES 依赖断言不受影响，仍由各测试直接检查 CMakeLists.txt 原文。
"""

import pathlib


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN_DIR = REPO_ROOT / "main"
MAIN_CMAKE = MAIN_DIR / "CMakeLists.txt"


def _glob_rule_for(rel_path: str) -> str:
    """返回 main/CMakeLists.txt 中应覆盖 rel_path 的 glob 模式字符串。

    rel_path 是相对 main/ 的源文件路径，如 "services/ota/ota_service.c"。
    模式必须与 CMakeLists.txt 中的 glob 规则逐字一致，否则测试失败提示改错位置。
    """
    parts = rel_path.split("/")
    top = parts[0]
    if top == "app":
        return "${CMAKE_CURRENT_LIST_DIR}/app/*.c"
    if top == "services":
        suffix = ".cc" if rel_path.endswith(".cc") else ".c"
        return f"${{CMAKE_CURRENT_LIST_DIR}}/services/*{suffix}"
    if top == "features":
        return "${CMAKE_CURRENT_LIST_DIR}/features/*.c"
    if top == "ui":
        if len(parts) == 2:
            return "${CMAKE_CURRENT_LIST_DIR}/ui/*.c"
        if parts[1] == "generated":
            return "${CMAKE_CURRENT_LIST_DIR}/ui/generated/*.c"
        if parts[1] == "custom":
            if len(parts) == 3:
                return "${CMAKE_CURRENT_LIST_DIR}/ui/custom/*.c"
            if parts[2] == "fonts":
                return "${CMAKE_CURRENT_LIST_DIR}/ui/custom/fonts/*.c"
    raise AssertionError(f"无法映射到 glob 规则的 main 源文件路径: {rel_path}")


def assert_main_source_globbed(testcase, rel_path: str) -> None:
    """断言 main 组件源文件存在且会被 CMakeLists.txt 的 glob 规则自动编译。

    Args:
        testcase: unittest.TestCase 实例。
        rel_path: 相对 main/ 的源文件路径，如 "services/ota/ota_service.c"。
    """
    source = MAIN_DIR / rel_path
    testcase.assertTrue(
        source.exists(),
        f"main/{rel_path} 应存在；若文件已删除，请同步移除对应测试断言",
    )

    cmake = MAIN_CMAKE.read_text(encoding="utf-8")
    pattern = _glob_rule_for(rel_path)
    testcase.assertIn(
        pattern,
        cmake,
        f"main/CMakeLists.txt 应包含覆盖 {rel_path} 的 glob 规则 {pattern}",
    )
