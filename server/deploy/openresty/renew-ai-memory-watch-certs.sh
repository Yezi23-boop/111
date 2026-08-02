#!/usr/bin/env bash
set -eu

ssl_root=/opt/1panel/apps/openresty/openresty/conf/ssl
for host in hermes.934000.xyz watch.934000.xyz; do
    install -d -m 755 "$ssl_root/$host"
    install -m 644 "/etc/letsencrypt/live/$host/fullchain.pem" "$ssl_root/$host/fullchain.pem"
    install -m 600 "/etc/letsencrypt/live/$host/privkey.pem" "$ssl_root/$host/privkey.pem"
done

container_name="$(docker ps --filter 'name=1Panel-openresty-' --format '{{.Names}}' | head -n 1)"
if [ -n "$container_name" ]; then
    docker exec "$container_name" openresty -t
    docker exec "$container_name" openresty -s reload
fi
