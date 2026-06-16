param(
  [string]$BaseUrl = "http://127.0.0.1:8787",
  [string]$EnvFile = "D:\Docker_data\hermes\watch_voice_endpoint.env",
  [string]$HermesUrl = "http://127.0.0.1:8642",
  [string]$HermesEnvFile = "D:\Docker_data\hermes\data\.env",
  [string]$DeviceId = "watch-001",
  [string]$SampleText = "记一下明天看电池日志",
  [switch]$SkipDocker,
  [switch]$SkipHermesApi,
  [switch]$SkipServiceHealth,
  [switch]$SkipRealAsr,
  [switch]$AssertPrivateNotExposed
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$runtimeStatusScript = Join-Path $scriptRoot "runtime_status.ps1"
$smokeTestScript = Join-Path $scriptRoot "smoke_test.ps1"
$hermesTextSmokeScript = Join-Path $scriptRoot "hermes_text_smoke.ps1"
$ttsScript = Join-Path $scriptRoot "make_tts_sample.ps1"

function Convert-JsonOutput {
  param(
    [Parameter(Mandatory = $true)]
    [object[]]$Output
  )

  return ($Output | Out-String | ConvertFrom-Json)
}

function Select-SmokeSummary {
  param(
    [Parameter(Mandatory = $true)]
    [object]$Smoke
  )

  return [pscustomobject]@{
    watch_health = $Smoke.watch_health
    hermes_status = $Smoke.hermes_status
    asr_mode = $Smoke.asr_mode
    voice_status = $Smoke.voice_status
    voice_action = $Smoke.voice_action
    text_status = $Smoke.text_status
    text_action = $Smoke.text_action
    cancel_status = $Smoke.cancel_status
    cancel_action = $Smoke.cancel_action
    auth_failure_status_code = $Smoke.auth_failure_status_code
    asr_text_present = $Smoke.asr_text_present
    reply_text_present = $Smoke.reply_text_present
    asr_text_chars = $Smoke.asr_text_chars
    reply_text_chars = $Smoke.reply_text_chars
    text_reply_present = $Smoke.text_reply_present
    text_reply_chars = $Smoke.text_reply_chars
    field_count = $Smoke.field_count
  }
}

function Select-HermesTextSummary {
  param(
    [Parameter(Mandatory = $true)]
    [object]$Smoke
  )

  return [pscustomobject]@{
    health_status = $Smoke.health_status
    model_count = $Smoke.model_count
    first_model = $Smoke.first_model
    response_status = $Smoke.response_status
    response_id_present = $Smoke.response_id_present
    output_text_present = $Smoke.output_text_present
    output_text_chars = $Smoke.output_text_chars
  }
}

function Select-RuntimeSummary {
  param(
    [Parameter(Mandatory = $true)]
    [object]$Runtime
  )

  $serviceHealth = $Runtime.endpoints.service_health
  return [pscustomobject]@{
    watch_voice_endpoint = $Runtime.docker.watch_voice_endpoint.status
    watch_voice_endpoint_health = $Runtime.docker.watch_voice_endpoint.health
    hermes = $Runtime.docker.hermes.status
    service_health_skipped = $serviceHealth.skipped
    service_health_ok = $serviceHealth.ok
    service_status = $serviceHealth.status
    watch_health_ok = $Runtime.endpoints.watch_health.ok
    hermes_health_ok = $Runtime.endpoints.hermes_health.ok
    hermes_model_count = $Runtime.endpoints.hermes_models.model_count
    asr_provider = $serviceHealth.asr_provider
    request_timeout_seconds = $serviceHealth.request_timeout_seconds
    inflight_requests = $serviceHealth.inflight_requests
    request_events = $serviceHealth.request_events
    request_status_counts = $serviceHealth.request_status_counts
    auth_failures = $serviceHealth.auth_failures
    last_auth_failure = $serviceHealth.last_auth_failure
    private_exposure = $Runtime.endpoints.private_exposure
  }
}

function Select-EndpointErrors {
  param(
    [Parameter(Mandatory = $true)]
    [object]$Runtime
  )

  return [pscustomobject]@{
    service_health = $Runtime.endpoints.service_health.error
    private_exposure = $Runtime.endpoints.private_exposure
    watch_health = $Runtime.endpoints.watch_health.error
    hermes_health = $Runtime.endpoints.hermes_health.error
    hermes_models = $Runtime.endpoints.hermes_models.error
  }
}

function Get-PreflightFailureReason {
  param(
    [Parameter(Mandatory = $true)]
    [object]$Runtime
  )

  $reasons = @()
  if (-not $Runtime.env.watch_device_token_present) {
    $reasons += "watch_device_token_missing"
  }
  if ((-not $SkipServiceHealth) -and (-not $Runtime.endpoints.service_health.ok)) {
    $reasons += "service_health_unreachable"
  }
  if (-not $Runtime.endpoints.watch_health.ok) {
    $reasons += "watch_health_unreachable"
  }
  if ((-not $SkipHermesApi) -and (-not $Runtime.endpoints.hermes_health.ok)) {
    $reasons += "hermes_health_unreachable"
  }
  if ((-not $SkipHermesApi) -and ($Runtime.endpoints.hermes_models.model_count -lt 1)) {
    $reasons += "hermes_models_unavailable"
  }
  if ($AssertPrivateNotExposed -and (-not $Runtime.endpoints.private_exposure.ok)) {
    $reasons += "private_path_unexpected_status"
  }

  if ($reasons.Count -eq 0) {
    return $null
  }
  return $reasons -join ";"
}

function Write-AcceptanceFailure {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Reason,
    [Parameter(Mandatory = $true)]
    [object]$Runtime
  )

  [pscustomobject]@{
    status = "failed"
    reason = $Reason
    base_url = $BaseUrl
    device_id = $DeviceId
    generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
    runtime_before = Select-RuntimeSummary -Runtime $Runtime
    endpoint_errors = Select-EndpointErrors -Runtime $Runtime
  } | ConvertTo-Json -Depth 8
  exit 1
}

$statusArgs = @{
  BaseUrl = $BaseUrl
  HermesUrl = $HermesUrl
  WatchEnvFile = $EnvFile
  HermesEnvFile = $HermesEnvFile
  DeviceId = $DeviceId
}
if ($SkipDocker) {
  $statusArgs.SkipDocker = $true
}
if ($SkipHermesApi) {
  $statusArgs.SkipHermesApi = $true
}
if ($SkipServiceHealth) {
  $statusArgs.SkipServiceHealth = $true
}
if ($AssertPrivateNotExposed) {
  $statusArgs.AssertPrivateNotExposed = $true
}

$smokeArgs = @{
  BaseUrl = $BaseUrl
  EnvFile = $EnvFile
  DeviceId = $DeviceId
  MockAsrText = $SampleText
  TextCommand = $SampleText
  IncludeCancel = $true
  IncludeAuthFailure = $true
  IncludeTextCommand = $true
}
if ($SkipServiceHealth) {
  $smokeArgs.SkipServiceHealth = $true
}

$before = Convert-JsonOutput -Output (& $runtimeStatusScript @statusArgs)
$preflightFailure = Get-PreflightFailureReason -Runtime $before
if (-not [string]::IsNullOrWhiteSpace($preflightFailure)) {
  Write-AcceptanceFailure -Reason $preflightFailure -Runtime $before
}

$hermesText = $null
if (-not $SkipHermesApi) {
  $hermesText = Convert-JsonOutput -Output (& $hermesTextSmokeScript -HermesUrl $HermesUrl -HermesEnvFile $HermesEnvFile)
}
$mock = Convert-JsonOutput -Output (& $smokeTestScript @smokeArgs)

$sample = $null
$real = $null
if (-not $SkipRealAsr) {
  $sample = Convert-JsonOutput -Output (& $ttsScript -Text $SampleText)
  $realArgs = @{
    BaseUrl = $BaseUrl
    EnvFile = $EnvFile
    DeviceId = $DeviceId
    UseRealAsr = $true
    AudioPath = $sample.ogg_path
    IncludeCancel = $true
  }
  if ($SkipServiceHealth) {
    $realArgs.SkipServiceHealth = $true
  }
  $real = Convert-JsonOutput -Output (& $smokeTestScript @realArgs)
}

$after = Convert-JsonOutput -Output (& $runtimeStatusScript @statusArgs)

[pscustomobject]@{
  status = "passed"
  base_url = $BaseUrl
  device_id = $DeviceId
  generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
  runtime_before = Select-RuntimeSummary -Runtime $before
  hermes_text_smoke = $(if ($hermesText) { Select-HermesTextSummary -Smoke $hermesText } else { $null })
  mock_smoke = Select-SmokeSummary -Smoke $mock
  real_asr_smoke = $(if ($real) { Select-SmokeSummary -Smoke $real } else { $null })
  real_asr_sample = $(if ($sample) {
      [pscustomobject]@{
        voice = $sample.voice
        ogg_bytes = $sample.ogg_bytes
      }
    } else {
      $null
    })
  runtime_after = Select-RuntimeSummary -Runtime $after
} | ConvertTo-Json -Depth 8
