#!/bin/sh
set -eu

SERVICE=watch-relay-autossh.service

restart_relay() {
  logger -t watch-relay-healthcheck "relay unavailable; restarting ${SERVICE}"
  systemctl restart "$SERVICE"
}

if ! systemctl is-active --quiet "$SERVICE"; then
  restart_relay
  exit 0
fi

listener_count=$(ss -ltnH | awk '$4 ~ /127\.0\.0\.1:(18787|19119)$/ {count++} END {print count+0}')
if [ "$listener_count" -lt 2 ]; then
  restart_relay
  exit 0
fi

watch_ok=0
hermes_ok=0
curl --silent --show-error --fail --max-time 4 \
  http://127.0.0.1:18787/health >/dev/null 2>&1 && watch_ok=1
curl --silent --show-error --fail --location --max-time 4 \
  http://127.0.0.1:19119/chat >/dev/null 2>&1 && hermes_ok=1

if [ "$watch_ok" -eq 0 ] && [ "$hermes_ok" -eq 0 ]; then
  restart_relay
fi
