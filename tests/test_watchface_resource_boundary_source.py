from tests.main_paths import REPO_ROOT


EXTRACT_SCRIPT = REPO_ROOT / "scripts" / "watchface" / "extract_watchface_frames.py"
PACK_SCRIPT = REPO_ROOT / "scripts" / "watchface" / "pack_watchface_rawanim.py"
RESOURCES_WATCHFACE_DIR = REPO_ROOT / "resources" / "watchface"


def test_watchface_intermediate_frames_do_not_live_in_resources_partition() -> None:
    assert not RESOURCES_WATCHFACE_DIR.exists()


def test_watchface_scripts_default_to_sdcard_staging_paths() -> None:
    extract_source = EXTRACT_SCRIPT.read_text(encoding="utf-8")
    pack_source = PACK_SCRIPT.read_text(encoding="utf-8")

    assert 'DEFAULT_OUTPUT_DIR = Path("sdcard/watchface/frames")' in extract_source
    assert 'DEFAULT_INPUT = Path("sdcard/watchface/frames")' in pack_source
    assert 'DEFAULT_SD_OUTPUT = Path("sdcard/watchface")' in pack_source
    assert 'Path("resources/watchface' not in extract_source
    assert 'Path("resources/watchface' not in pack_source
