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
  [switch]$SkipRealAsr
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$runtimeStatusScript = Join-Path $scriptRoot "runtime_status.ps1"
$smokeTestScript = Join-Path $scriptRoot "smoke_test.ps1"
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
    cancel_status = $Smoke.cancel_status
    cancel_action = $Smoke.cancel_action
    auth_failure_status_code = $Smoke.auth_failure_status_code
    field_count = $Smoke.field_count
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
  }
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

$smokeArgs = @{
  BaseUrl = $BaseUrl
  EnvFile = $EnvFile
  DeviceId = $DeviceId
  MockAsrText = $SampleText
  IncludeCancel = $true
  IncludeAuthFailure = $true
}
if ($SkipServiceHealth) {
  $smokeArgs.SkipServiceHealth = $true
}

$before = Convert-JsonOutput -Output (& $runtimeStatusScript @statusArgs)
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
