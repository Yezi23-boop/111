param(
  [string]$BaseUrl = "http://127.0.0.1:8787",
  [string]$EnvFile = "D:\Docker_data\hermes\watch_voice_endpoint.env",
  [string]$DeviceId = "watch-001",
  [string]$MockAsrText = "记一下明天看电池日志",
  [string]$AudioPath = "",
  [switch]$UseRealAsr,
  [switch]$SkipServiceHealth
)

$ErrorActionPreference = "Stop"

function Get-WatchDeviceToken {
  param(
    [string]$Path,
    [string]$TargetDeviceId
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    throw "Env file not found: $Path"
  }

  $line = Get-Content -LiteralPath $Path |
    Where-Object { $_ -match "^WATCH_DEVICE_TOKENS=" } |
    Select-Object -First 1
  if ([string]::IsNullOrWhiteSpace($line)) {
    throw "WATCH_DEVICE_TOKENS is missing in $Path"
  }

  $raw = $line -replace "^WATCH_DEVICE_TOKENS=", ""
  foreach ($pair in ($raw -split ",")) {
    $parts = $pair -split "=", 2
    if ($parts.Count -eq 2 -and $parts[0].Trim() -eq $TargetDeviceId) {
      return $parts[1].Trim()
    }
  }

  throw "Device token not found for $TargetDeviceId"
}

$token = Get-WatchDeviceToken -Path $EnvFile -TargetDeviceId $DeviceId
$headers = @{ Authorization = "Bearer $token" }

$localHealthStatus = "skipped"
if (-not $SkipServiceHealth) {
  $localHealth = Invoke-RestMethod -Uri "$BaseUrl/health" -TimeoutSec 5
  $localHealthStatus = $localHealth.status
}
$watchHealth = Invoke-RestMethod -Uri "$BaseUrl/v1/watch/health?device_id=$DeviceId" -Headers $headers -TimeoutSec 10

if ($UseRealAsr -and [string]::IsNullOrWhiteSpace($AudioPath)) {
  throw "AudioPath is required when UseRealAsr is set"
}

if ([string]::IsNullOrWhiteSpace($AudioPath)) {
  $tmpAudio = Join-Path $env:TEMP "watch-smoke-test-$([guid]::NewGuid().ToString('N')).opus"
  [byte[]]$bytes = 0x4F, 0x67, 0x67, 0x53, 0x00, 0x01, 0x02, 0x03
  [System.IO.File]::WriteAllBytes($tmpAudio, $bytes)
  $AudioPath = $tmpAudio
}

$form = @{
  request_id = "smoke-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
  device_id = $DeviceId
  audio = Get-Item -LiteralPath $AudioPath
}

if (-not $UseRealAsr) {
  $form["mock_asr_text"] = $MockAsrText
}

$voice = Invoke-RestMethod -Uri "$BaseUrl/v1/watch/voice-command" `
  -Method Post `
  -Headers $headers `
  -Form $form `
  -TimeoutSec 130

$fields = @("request_id", "status", "action", "asr_text", "reply_text", "clarification_id", "error_code")
$missing = @($fields | Where-Object { -not ($voice.PSObject.Properties.Name -contains $_) })
if ($missing.Count -gt 0) {
  throw "Missing watch response fields: $($missing -join ', ')"
}

[pscustomobject]@{
  local_health = $localHealthStatus
  watch_health = $watchHealth.status
  hermes_status = $watchHealth.hermes_status
  asr_mode = $(if ($UseRealAsr) { "real" } else { "mock" })
  voice_status = $voice.status
  voice_action = $voice.action
  asr_text = $voice.asr_text
  reply_text = $voice.reply_text
  field_count = $fields.Count
} | ConvertTo-Json -Depth 4
