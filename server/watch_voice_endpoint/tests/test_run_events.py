from run_events import SseLineParser, normalize_run_event


def test_normalize_run_event_maps_watch_phases_without_raw_text():
    assert normalize_run_event(
        {"event": "tool.start", "tool_name": "web_search"}
    ).phase == normalize_run_event(
        {"event": "tool.progress", "tool": {"name": "web_search"}}
    ).phase
    assert normalize_run_event({"event": "tool.start", "tool_name": "shell"}).phase == "executing"
    assert normalize_run_event({"event": "message.delta"}).phase == "composing"
    assert normalize_run_event({"event": "message.complete"}).terminal is True


def test_sse_parser_ignores_keepalive_and_parses_data_frame():
    parser = SseLineParser()
    assert parser.feed_line(": keepalive") is None
    assert parser.feed_line('data: {"event":"tool.start","tool_name":"search"}') is None
    event = parser.feed_line("")
    assert event is not None
    assert event.name == "tool.start"
    assert event.phase == "searching"


def test_unknown_or_invalid_sse_payload_is_ignored():
    parser = SseLineParser()
    parser.feed_line("data: not-json")
    assert parser.feed_line("") is None
    assert normalize_run_event({"event": "unknown.event"}) is None
