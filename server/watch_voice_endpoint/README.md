# AI Memory Watch Voice Endpoint

This is the narrow server-side adapter between the ESP32-S3 watch and Hermes.

Flow:

```text
ESP32-S3 watch -> /v1/watch/voice-command -> Hermes /v1/responses -> watch JSON
```

V1 keeps ASR on the server side so the watch can validate Ogg Opus upload, auth, timeout, cancel, and text rendering without changing the ESP32 protocol. The adapter can run in fast mock mode or MiMo ASR mode through `WATCH_ASR_PROVIDER`.

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

## Request Contract

The machine-readable ESP32-facing V1 contract lives in [watch_contract.v1.json](watch_contract.v1.json). Keep it in sync with `app.py`; `tests/test_contract.py` locks the response fields, enums, request limits, and timeout budget against the FastAPI model.

`request_id` must be 1-96 ASCII characters using only letters, numbers, `.`, `_`, `:`, or `-`.

For V1, normal bad watch input such as an invalid `request_id`, an empty upload, or an audio body larger than `WATCH_MAX_AUDIO_BYTES` returns the same seven-field watch JSON with `status=error` and `error_code=asr_or_agent_error`. Device authentication failures still use HTTP 401/403.

`WATCH_REQUEST_TIMEOUT_SECONDS` caps the whole ASR + Hermes request. Keep it below the ESP32 wait window; the default is `115` seconds so the server can return `status=timeout` and `error_code=server_timeout` before the watch-side 120 second timer expires.

## Request Idempotency

`POST /v1/watch/voice-command` is keyed by `device_id + request_id`.

- A repeated completed request returns the cached watch JSON.
- A repeated in-flight request waits for the same processing task instead of calling ASR/Hermes again.
- A canceled request returns `status=canceled` and `action=no_action`.
- Cancel after completion returns the completed result, because server-side tools may already have run.

This keeps ESP32 retry behavior from duplicating memory or reminder actions during unstable Wi-Fi.

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

## Source Tests

Run the server source tests from the repo root:

```powershell
uv run --with pytest==8.3.4 --with fastapi==0.115.6 --with httpx==0.28.1 --with python-multipart==0.0.20 --with "uvicorn[standard]==0.34.0" `
  python -m pytest server/watch_voice_endpoint/tests -q
```

## ASR Provider

Default ASR is mocked:

```text
WATCH_ASR_PROVIDER=mock
```

MiMo ASR can be enabled without changing the watch protocol:

```text
WATCH_ASR_PROVIDER=mimo
MIMO_ASR_BASE_URL=https://token-plan-cn.xiaomimimo.com/v1
MIMO_ASR_API_KEY=<redacted>
MIMO_ASR_MODEL=mimo-v2.5-asr
MIMO_ASR_LANGUAGE=auto
```

MiMo ASR currently accepts WAV/MP3 style MIME types, so the endpoint transcodes watch Ogg Opus uploads to 16 kHz mono WAV with ffmpeg before sending a base64 `input_audio` data URI to the OpenAI-compatible `/chat/completions` endpoint. The resulting text is then passed to Hermes `/v1/responses`.

## Persistent Local Smoke Test

Current local联调 uses:

```powershell
D:\Docker_data\hermes\watch_voice_endpoint.env
```

That file stores `HERMES_API_KEY` and `WATCH_DEVICE_TOKENS`, so keep it outside git.
Do not paste `docker compose config` output into logs or issues because Docker Compose expands `env_file` values.

Start a persistent Docker Desktop container:

```powershell
docker compose -f compose.local.yml up -d --build
```

Verify the full server-side path without printing tokens:

```powershell
.\hermes_text_smoke.ps1
.\smoke_test.ps1
.\smoke_test.ps1 -IncludeCancel
.\smoke_test.ps1 -IncludeCancel -IncludeAuthFailure
```

`hermes_text_smoke.ps1` directly verifies Hermes API Server `/health`, `/v1/models`, and `/v1/responses` with `API_SERVER_KEY` loaded from `D:\Docker_data\hermes\data\.env`. It reports only non-secret status, model count, response status, and output length.

The smoke script rejects missing or extra watch response fields, so both voice and optional cancel checks must return exactly the V1 seven-field JSON shape.
It also fails by default unless watch health is `ok/online`, voice completes with `status=done`, and optional cancel returns `canceled/no_action`. Use `-AllowErrorResponse` only for deliberate failure-shape debugging.
With `-IncludeAuthFailure`, it also verifies that an invalid device token is rejected with HTTP 403 without printing any token.
By default, smoke output redacts `asr_text` and `reply_text`; it only reports whether text is present and the character counts. Use `-IncludeText` only for local manual debugging when the transcript and reply are safe to show.

Inspect local runtime status without printing tokens:

```powershell
.\runtime_status.ps1
```

`/health` and `runtime_status.ps1` expose non-secret request metrics for debugging: event counters, response status counters, error-code counters, and the latest request summary with status, action, duration, and audio byte size. They do not include ASR text, reply text, audio contents, API keys, or bearer tokens.
If Docker inspect is blocked by the current shell permissions, container status is reported as `inspect_unavailable`; use the endpoint checks in the same output as the authoritative service availability signal.

Run the full local acceptance loop before server-side iteration commits:

```powershell
.\acceptance_test.ps1
```

This composes runtime status, direct Hermes text smoke, mock voice, cancel, invalid-token rejection, generated Chinese Ogg Opus, and real MiMo ASR smoke checks. Its summary omits ASR text, reply text, audio contents, API keys, and bearer tokens.
If required endpoints are offline, it exits with `status=failed` and a non-secret preflight summary instead of a raw PowerShell REST stack trace.

Run the release gate before cutting or deploying a server-side iteration:

```powershell
.\release_gate.ps1
.\release_gate.ps1 -RebuildContainer
```

The release gate runs server pytest first, then the local acceptance loop. With `-RebuildContainer`, it rebuilds the persistent local Docker container before acceptance. The output is a non-secret JSON summary: test tail, runtime counters, smoke statuses, actions, text-present flags, text lengths, and field counts only.

Verify real ASR with an Ogg Opus sample:

```powershell
$sample = .\make_tts_sample.ps1 | ConvertFrom-Json
.\smoke_test.ps1 -UseRealAsr -AudioPath $sample.ogg_path
.\smoke_test.ps1 -UseRealAsr -AudioPath $sample.ogg_path -IncludeCancel
```

For a public domain that only proxies `/v1/watch/*`, skip the private service health endpoint:

```powershell
.\smoke_test.ps1 -BaseUrl "https://watch.example.com" -SkipServiceHealth
.\runtime_status.ps1 -BaseUrl "https://watch.example.com" -SkipDocker -SkipHermesApi -SkipServiceHealth
.\acceptance_test.ps1 -BaseUrl "https://watch.example.com" -SkipDocker -SkipHermesApi -SkipServiceHealth -SkipRealAsr
.\release_gate.ps1 -BaseUrl "https://watch.example.com" -SkipDocker -SkipHermesApi -SkipServiceHealth -SkipRealAsr
```

Manual `docker run` is still useful for one-off isolation:

```powershell
docker run -d --name ai-memory-watch-voice-endpoint --restart unless-stopped `
  -p 127.0.0.1:8787:8787 `
  --env-file D:\Docker_data\hermes\watch_voice_endpoint.env `
  ai-memory-watch-voice-endpoint:dev
```

## Domain And TLS

Use [deploy/Caddyfile.example](deploy/Caddyfile.example) as the first public-domain shape:

```text
ESP32-S3 -> https://watch.example.com/v1/watch/*
          -> Caddy
          -> http://127.0.0.1:8787/v1/watch/*
```

Keep these private:

```text
Hermes API Server :8642
Hermes Dashboard  :9119
```

The ESP32 should only know the public watch endpoint URL and its `device_token`; it must not know `HERMES_API_KEY` or call the Hermes Dashboard.
