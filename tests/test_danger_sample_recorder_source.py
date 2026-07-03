from pathlib import Path

from tests.main_paths import DANGER_DETECTION_DIR


RECORDER_C = DANGER_DETECTION_DIR / "danger_sample_recorder.c"
RECORDER_H = DANGER_DETECTION_DIR / "danger_sample_recorder.h"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_capture_api_requires_window_end_sample_index() -> None:
    header = _read(RECORDER_H)
    source = _read(RECORDER_C)

    assert "danger_sample_recorder_capture(uint32_t label_index, float confidence,\n                                         uint64_t window_end_sample_index)" in header
    assert "uint64_t window_end_sample_index)" in source
    assert "window_end_sample_index - pre_samples" in source


def test_capture_enters_pending_state_for_post_buffer() -> None:
    source = _read(RECORDER_C)

    assert "pending_capture_t" in source
    assert ".post_samples = post_samples" in source
    assert ".post_collected = initial_post_samples" in source
    assert "collect_pending_post_samples_locked" in source
    assert "pending->post_collected < pending->post_samples" in source
    assert "queue_completed_capture" in source
    assert "ring_buffer_read_ordered" not in source


def test_capture_backfills_post_samples_already_in_ring() -> None:
    source = _read(RECORDER_C)

    assert "initial_post_samples" in source
    assert "s_recorder.next_sample_index > window_end_sample_index" in source
    assert "available_post_end" in source
    assert "ring_buffer_copy_range(&s_recorder.ring_buffer, oldest_sample,\n                                    window_end_sample_index,\n                                    &temp_buffer[pre_samples],\n                                    initial_post_samples)" in source
    assert "if (initial_post_samples >= post_samples)" in source
    assert "completed_request = (write_request_t)" in source
    assert "should_queue = true" in source


def test_pcm_tap_tracks_continuous_sample_indices() -> None:
    source = _read(RECORDER_C)

    assert "chunk_start = meta->absolute_sample_index" in source
    assert "chunk_start != s_recorder.next_sample_index" in source
    assert "ring_buffer_write(&s_recorder.ring_buffer, pcm_data, samples)" in source
    assert "s_recorder.next_sample_index = chunk_start + samples" in source


def test_reset_session_invalidates_generation_and_clears_pending_capture() -> None:
    header = _read(RECORDER_H)
    source = _read(RECORDER_C)

    assert "danger_sample_recorder_reset_session" in header
    assert "void danger_sample_recorder_reset_session(void)" in source
    reset_body = source[
        source.index("void danger_sample_recorder_reset_session(void)") :
        source.index("esp_err_t danger_sample_recorder_capture")
    ]
    assert "s_recorder.runtime_generation++" in reset_body
    assert "ring_buffer_reset(&s_recorder.ring_buffer)" in reset_body
    assert "clear_pending_capture_locked()" in reset_body


def test_tmp_files_are_committed_with_sd_manager_rename() -> None:
    source = _read(RECORDER_C)

    assert "sd_manager_rename_file(tmp_path, filepath)" in source
    assert '"%s.tmp", filepath' in source
    assert source.count("sd_manager_rename_file(tmp_path, filepath)") >= 2
    assert "window_end_sample_index" in source
    assert "pre_ms" in source
    assert "post_ms" in source
