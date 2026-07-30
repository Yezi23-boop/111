#!/bin/sh
set -eu

STATE_DIR=/run/ai-memory-watch-watchdog
FAILURE_LIMIT=3
mkdir -p "$STATE_DIR"

log_memory_snapshot() {
  available_kb=$(awk '/MemAvailable:/ {print $2}' /proc/meminfo)
  stats=$(timeout 8 docker stats --no-stream --format '{{.Name}}={{.MemUsage}}' \
    hermes ai-memory-watch-voice-endpoint 2>/dev/null | tr '\n' ' ' || true)
  logger -t watch-cloud-healthcheck "mem_available_kb=${available_kb} ${stats}"
}

check_container() {
  container=$1
  url=$2
  state_file="$STATE_DIR/$container.failures"
  failures=0
  [ -f "$state_file" ] && failures=$(cat "$state_file")

  if curl --silent --show-error --fail --max-time 5 "$url" >/dev/null 2>&1; then
    printf '0\n' >"$state_file"
    return
  fi

  failures=$((failures + 1))
  printf '%s\n' "$failures" >"$state_file"
  logger -t watch-cloud-healthcheck \
    "container=${container} health_failed count=${failures}/${FAILURE_LIMIT}"

  if [ "$failures" -ge "$FAILURE_LIMIT" ]; then
    docker restart "$container" >/dev/null
    printf '0\n' >"$state_file"
    logger -t watch-cloud-healthcheck "container=${container} restarted"
  fi
}

log_memory_snapshot
check_container hermes http://127.0.0.1:8642/health
check_container ai-memory-watch-voice-endpoint http://127.0.0.1:8787/health
