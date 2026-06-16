param(
  [string]$BaseUrl = "http://127.0.0.1:8787",
  [string]$EnvFile = "D:\Docker_data\hermes\watch_voice_endpoint.env",
  [string]$DeviceId = "watch-001",
  [string]$MockAsrText = "记一下明天看电池日志",
  [string]$TextCommand = "记一下明天看电池日志",
  [string]$AudioPath = "",
  [switch]$UseRealAsr,
  [switch]$SkipServiceHealth,
  [switch]$IncludeCancel,
  [switch]$IncludeAuthFailure,
  [switch]$IncludeTextCommand,
  [switch]$IncludeText,
  [switch]$AllowErrorResponse
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
$createdTempAudio = $null

try {
  $localHealthStatus = "skipped"
  if (-not $SkipServiceHealth) {
    $localHealth = Invoke-RestMethod -Uri "$BaseUrl/health" -TimeoutSec 5
    $localHealthStatus = $localHealth.status
  }
  $watchHealth = Invoke-RestMethod -Uri "$BaseUrl/v1/watch/health?device_id=$DeviceId" -Headers $headers -TimeoutSec 10

  $authFailureStatusCode = $null
  if ($IncludeAuthFailure) {
    try {
      Invoke-RestMethod -Uri "$BaseUrl/v1/watch/health?device_id=$DeviceId" `
        -Headers @{ Authorization = "Bearer invalid-smoke-token" } `
        -TimeoutSec 10 | Out-Null
      throw "Invalid device token unexpectedly succeeded"
    } catch {
      $response = $_.Exception.Response
      if ($null -eq $response) {
        throw
      }
      $authFailureStatusCode = [int]$response.StatusCode
      if ($authFailureStatusCode -ne 403) {
        throw "Expected invalid device token to return 403, got $authFailureStatusCode"
      }
    }
  }

  if ($UseRealAsr -and [string]::IsNullOrWhiteSpace($AudioPath)) {
    throw "AudioPath is required when UseRealAsr is set"
  }

  if ([string]::IsNullOrWhiteSpace($AudioPath)) {
    $tmpAudio = Join-Path $env:TEMP "watch-smoke-test-$([guid]::NewGuid().ToString('N')).opus"
    [byte[]]$bytes = 0x4F, 0x67, 0x67, 0x53, 0x00, 0x01, 0x02, 0x03
    [System.IO.File]::WriteAllBytes($tmpAudio, $bytes)
    $createdTempAudio = $tmpAudio
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

  function Assert-WatchResponseFields {
    param(
      [Parameter(Mandatory = $true)]
      [object]$Payload,
      [Parameter(Mandatory = $true)]
      [string]$Label
    )

    $names = @($Payload.PSObject.Properties.Name)
    $missing = @($fields | Where-Object { -not ($names -contains $_) })
    $extra = @($names | Where-Object { -not ($fields -contains $_) })
    if ($missing.Count -gt 0 -or $extra.Count -gt 0) {
      throw "$Label response field mismatch. Missing: $($missing -join ', '); Extra: $($extra -join ', ')"
    }
  }

  function Assert-WatchHealthReady {
    param(
      [Parameter(Mandatory = $true)]
      [object]$Payload
    )

    if ($AllowErrorResponse) {
      return
    }
    if ($Payload.status -ne "ok" -or $Payload.hermes_status -ne "online") {
      throw "Watch health is not ready. status=$($Payload.status); hermes_status=$($Payload.hermes_status)"
    }
  }

  function Assert-VoiceSucceeded {
    param(
      [Parameter(Mandatory = $true)]
      [object]$Payload
    )

    if ($AllowErrorResponse) {
      return
    }
    if ($Payload.status -ne "done" -or $Payload.action -eq "error") {
      throw "Voice smoke did not complete. status=$($Payload.status); action=$($Payload.action); error_code=$($Payload.error_code)"
    }
  }

  function Assert-CancelSucceeded {
    param(
      [Parameter(Mandatory = $true)]
      [object]$Payload
    )

    if ($AllowErrorResponse) {
      return
    }
    if ($Payload.status -ne "canceled" -or $Payload.action -ne "no_action") {
      throw "Cancel smoke did not cancel. status=$($Payload.status); action=$($Payload.action); error_code=$($Payload.error_code)"
    }
  }

  Assert-WatchHealthReady -Payload $watchHealth
  Assert-WatchResponseFields -Payload $voice -Label "voice"
  Assert-VoiceSucceeded -Payload $voice

  $textResponse = $null
  if ($IncludeTextCommand) {
    $textForm = @{
      request_id = "text-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
      device_id = $DeviceId
      text = $TextCommand
      locale = "zh-CN"
      timezone = "Asia/Shanghai"
      source = "watch_hermes_page"
      ui_state = "ready"
    }
    $textResponse = Invoke-RestMethod -Uri "$BaseUrl/v1/watch/text-command" `
      -Method Post `
      -Headers $headers `
      -Form $textForm `
      -TimeoutSec 130
    Assert-WatchResponseFields -Payload $textResponse -Label "text"
    Assert-VoiceSucceeded -Payload $textResponse
  }

  $cancel = $null
  if ($IncludeCancel) {
    $cancelRequestId = "cancel-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
    $cancel = Invoke-RestMethod -Uri "$BaseUrl/v1/watch/request/$cancelRequestId/cancel" `
      -Method Post `
      -Headers $headers `
      -Form @{ device_id = $DeviceId } `
      -TimeoutSec 15
    Assert-WatchResponseFields -Payload $cancel -Label "cancel"
    Assert-CancelSucceeded -Payload $cancel
  }

  $summary = [pscustomobject]@{
    local_health = $localHealthStatus
    watch_health = $watchHealth.status
    hermes_status = $watchHealth.hermes_status
    asr_mode = $(if ($UseRealAsr) { "real" } else { "mock" })
    voice_status = $voice.status
    voice_action = $voice.action
    text_status = $(if ($textResponse) { $textResponse.status } else { $null })
    text_action = $(if ($textResponse) { $textResponse.action } else { $null })
    cancel_status = $(if ($cancel) { $cancel.status } else { $null })
    cancel_action = $(if ($cancel) { $cancel.action } else { $null })
    auth_failure_status_code = $authFailureStatusCode
    asr_text_present = -not [string]::IsNullOrWhiteSpace($voice.asr_text)
    reply_text_present = -not [string]::IsNullOrWhiteSpace($voice.reply_text)
    asr_text_chars = $(if ($voice.asr_text) { $voice.asr_text.Length } else { 0 })
    reply_text_chars = $(if ($voice.reply_text) { $voice.reply_text.Length } else { 0 })
    text_reply_present = $(if ($textResponse) { -not [string]::IsNullOrWhiteSpace($textResponse.reply_text) } else { $null })
    text_reply_chars = $(if ($textResponse -and $textResponse.reply_text) { $textResponse.reply_text.Length } else { $null })
    field_count = $fields.Count
  }

  if ($IncludeText) {
    $summary | Add-Member -NotePropertyName asr_text -NotePropertyValue $voice.asr_text
    $summary | Add-Member -NotePropertyName reply_text -NotePropertyValue $voice.reply_text
    if ($textResponse) {
      $summary | Add-Member -NotePropertyName text_reply_text -NotePropertyValue $textResponse.reply_text
    }
  }

  $summary | ConvertTo-Json -Depth 4
} finally {
  if (-not [string]::IsNullOrWhiteSpace($createdTempAudio) -and
      (Test-Path -LiteralPath $createdTempAudio)) {
    Remove-Item -LiteralPath $createdTempAudio -Force -ErrorAction SilentlyContinue
  }
}
