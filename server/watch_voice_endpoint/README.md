# AI Memory Watch Voice Endpoint

This is the narrow server-side adapter between the ESP32-S3 watch and Hermes.

Flow:

```text
ESP32-S3 watch -> /v1/watch/voice-command -> Hermes /v1/responses -> watch JSON
```

V1 keeps ASR mocked so the watch can first validate Ogg Opus upload, auth, timeout, cancel, and text rendering. Real ASR can replace `_call_hermes` input preparation later without changing the ESP32 protocol.

## Endpoints

```http
GET  /v1/watch/health?device_id=watch-001
POST /v1/watch/voice-command
POST /v1/watch/request/{request_id}/cancel
```

All watch endpoints require:

```http
Authorization: Bearer <device_token>
```

The Hermes API key is only configured on this server through `HERMES_API_KEY`; it must not be written into ESP32 firmware.

## Local Run

```powershell
$env:HERMES_API_URL = "http://127.0.0.1:8642"
$env:HERMES_API_KEY = "<redacted>"
$env:WATCH_DEVICE_TOKENS = "watch-001=dev-watch-token"
uv run --with fastapi --with uvicorn --with httpx --with python-multipart `
  uvicorn app:app --host 127.0.0.1 --port 8787
```

## Docker Run

```powershell
docker build -t ai-memory-watch-voice-endpoint server/watch_voice_endpoint
docker run --rm -p 8787:8787 `
  --env-file server/watch_voice_endpoint/env.local `
  ai-memory-watch-voice-endpoint
```

For Docker on Windows, use `HERMES_API_URL=http://host.docker.internal:8642` when Hermes API Server is exposed on the host.
