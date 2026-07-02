from pathlib import Path

from tests.main_paths import REPO_ROOT


SD_MANAGER_C = REPO_ROOT / "components" / "sd_card" / "sd_manager.c"
SD_MANAGER_H = REPO_ROOT / "components" / "sd_card" / "sd_manager.h"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_sd_manager_file_api_declarations_have_implementations() -> None:
    header = _read(SD_MANAGER_H)
    source = _read(SD_MANAGER_C)

    for name in (
        "sd_manager_read_file",
        "sd_manager_write_file",
        "sd_manager_create_dir",
        "sd_manager_delete_file",
        "sd_manager_get_file_size",
    ):
        assert name in header
        assert f"esp_err_t {name}(" in source


def test_sd_manager_file_api_uses_vfs_stdio_and_stat() -> None:
    source = _read(SD_MANAGER_C)

    assert 'fopen(file_path, "rb")' in source
    assert "fread(buffer, 1, buffer_size, file)" in source
    assert 'fopen(file_path, "wb")' in source
    assert "fwrite(data, 1, data_size, file)" in source
    assert "mkdir(dir_path, 0775)" in source
    assert "remove(file_path)" in source
    assert "stat(file_path, &st)" in source
