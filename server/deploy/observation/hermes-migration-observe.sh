#!/usr/bin/env bash
set -eu

log_dir=/opt/ai-memory-watch/observations
log_file="$log_dir/migration.tsv"
mkdir -p "$log_dir"
touch "$log_file"
chmod 640 "$log_file"

timestamp="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
for name in ai-memory-watch-hermes ai-memory-watch-voice-endpoint ai-memory-watch-relay-connector; do
    status="$(docker inspect -f '{{.State.Status}}' "$name" 2>/dev/null || printf 'missing')"
    health="$(docker inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' "$name" 2>/dev/null || printf 'missing')"
    restarts="$(docker inspect -f '{{.RestartCount}}' "$name" 2>/dev/null || printf 'na')"
    printf '%s\tcontainer\t%s\t%s\t%s\t%s\n' "$timestamp" "$name" "$status" "$health" "$restarts" >> "$log_file"
done

hermes_probe=fail
watch_probe=fail
curl -fsS --max-time 5 http://127.0.0.1:8642/health >/dev/null && hermes_probe=ok || true
curl -fsS --max-time 5 http://127.0.0.1:8787/health >/dev/null && watch_probe=ok || true
printf '%s\tprobe\thermes=%s\twatch=%s\n' "$timestamp" "$hermes_probe" "$watch_probe" >> "$log_file"
