param(
  [string]$BaseUrl = "http://127.0.0.1:8787",
  [string]$HermesUrl = "http://127.0.0.1:8642",
  [string]$WatchEnvFile = "D:\Docker_data\hermes\watch_voice_endpoint.env",
  [string]$HermesEnvFile = "D:\Docker_data\hermes\data\.env",
  [string]$DeviceId = "watch-001",
  [switch]$SkipDocker,
  [switch]$SkipHermesApi
)

$ErrorActionPreference = "Stop"

function Get-EnvValue {
  param(
    [string]$Path,
    [string]$Key
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }

  $line = Get-Content -LiteralPath $Path |
    Where-Object { $_ -match "^$([regex]::Escape($Key))=" } |
    Select-Object -First 1
  if ([string]::IsNullOrWhiteSpace($line)) {
    return $null
  }

  return ($line -split "=", 2)[1]
}

function Test-EnvKey {
  param(
    [string]$Path,
    [string]$Key
  )

  $value = Get-EnvValue -Path $Path -Key $Key
  return -not [string]::IsNullOrWhiteSpace($value)
}

function Get-WatchDeviceToken {
  param(
    [string]$Path,
    [string]$TargetDeviceId
  )

  $raw = Get-EnvValue -Path $Path -Key "WATCH_DEVICE_TOKENS"
  if ([string]::IsNullOrWhiteSpace($raw)) {
    return $null
  }

  foreach ($pair in ($raw -split ",")) {
    $parts = $pair -split "=", 2
    if ($parts.Count -eq 2 -and $parts[0].Trim() -eq $TargetDeviceId) {
      return $parts[1].Trim()
    }
  }

  return $null
}

function Invoke-StatusGet {
  param(
    [string]$Uri,
    [hashtable]$Headers = @{},
    [int]$TimeoutSec = 10
  )

  try {
    $payload = Invoke-RestMethod -Uri $Uri -Headers $Headers -TimeoutSec $TimeoutSec
    return [pscustomobject]@{
      ok = $true
      error = $null
      payload = $payload
    }
  } catch {
    return [pscustomobject]@{
      ok = $false
      error = $_.Exception.Message
      payload = $null
    }
  }
}

function Get-ContainerStatus {
  param([string]$Name)

  try {
    $json = docker inspect $Name 2>$null | ConvertFrom-Json
    if (-not $json) {
      return [pscustomobject]@{
        exists = $false
        status = "missing"
        health = $null
      }
    }

    $container = $json[0]
    $health = $null
    if ($container.State.Health) {
      $health = $container.State.Health.Status
    }
    return [pscustomobject]@{
      exists = $true
      status = $container.State.Status
      health = $health
    }
  } catch {
    return [pscustomobject]@{
      exists = $false
      status = "unknown"
      health = $null
    }
  }
}

$watchToken = Get-WatchDeviceToken -Path $WatchEnvFile -TargetDeviceId $DeviceId
$watchHeaders = @{}
if (-not [string]::IsNullOrWhiteSpace($watchToken)) {
  $watchHeaders.Authorization = "Bearer $watchToken"
}

$hermesApiKey = Get-EnvValue -Path $HermesEnvFile -Key "API_SERVER_KEY"
$hermesHeaders = @{}
if (-not [string]::IsNullOrWhiteSpace($hermesApiKey)) {
  $hermesHeaders.Authorization = "Bearer $hermesApiKey"
}

$serviceHealth = Invoke-StatusGet -Uri "$BaseUrl/health" -TimeoutSec 5
$watchHealth = Invoke-StatusGet -Uri "$BaseUrl/v1/watch/health?device_id=$DeviceId" -Headers $watchHeaders -TimeoutSec 10

$hermesHealth = $null
$hermesModels = $null
if (-not $SkipHermesApi) {
  $hermesHealth = Invoke-StatusGet -Uri "$HermesUrl/health" -Headers $hermesHeaders -TimeoutSec 10
  $hermesModels = Invoke-StatusGet -Uri "$HermesUrl/v1/models" -Headers $hermesHeaders -TimeoutSec 10
}

$dockerStatus = $null
if (-not $SkipDocker) {
  $dockerStatus = [pscustomobject]@{
    hermes = Get-ContainerStatus -Name "hermes"
    watch_voice_endpoint = Get-ContainerStatus -Name "ai-memory-watch-voice-endpoint"
  }
}

$modelIds = @()
if ($hermesModels -and $hermesModels.ok -and $hermesModels.payload.data) {
  $modelIds = @($hermesModels.payload.data | ForEach-Object { $_.id })
}

[pscustomobject]@{
  generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
  base_url = $BaseUrl
  hermes_url = $HermesUrl
  device_id = $DeviceId
  env = [pscustomobject]@{
    watch_env_exists = Test-Path -LiteralPath $WatchEnvFile
    hermes_env_exists = Test-Path -LiteralPath $HermesEnvFile
    watch_device_token_present = -not [string]::IsNullOrWhiteSpace($watchToken)
    hermes_api_key_present = -not [string]::IsNullOrWhiteSpace($hermesApiKey)
    watch_asr_provider_present = Test-EnvKey -Path $WatchEnvFile -Key "WATCH_ASR_PROVIDER"
    watch_request_timeout_present = Test-EnvKey -Path $WatchEnvFile -Key "WATCH_REQUEST_TIMEOUT_SECONDS"
    mimo_asr_key_present = Test-EnvKey -Path $WatchEnvFile -Key "MIMO_ASR_API_KEY"
  }
  docker = $dockerStatus
  endpoints = [pscustomobject]@{
    service_health = [pscustomobject]@{
      ok = $serviceHealth.ok
      error = $serviceHealth.error
      status = $(if ($serviceHealth.payload) { $serviceHealth.payload.status } else { $null })
      asr_provider = $(if ($serviceHealth.payload) { $serviceHealth.payload.asr_provider } else { $null })
      request_timeout_seconds = $(if ($serviceHealth.payload) { $serviceHealth.payload.request_timeout_seconds } else { $null })
      inflight_requests = $(if ($serviceHealth.payload) { $serviceHealth.payload.inflight_requests } else { $null })
      request_events = $(if ($serviceHealth.payload) { $serviceHealth.payload.request_events } else { $null })
      request_status_counts = $(if ($serviceHealth.payload) { $serviceHealth.payload.request_status_counts } else { $null })
      request_error_counts = $(if ($serviceHealth.payload) { $serviceHealth.payload.request_error_counts } else { $null })
      last_request = $(if ($serviceHealth.payload) { $serviceHealth.payload.last_request } else { $null })
    }
    watch_health = [pscustomobject]@{
      ok = $watchHealth.ok
      error = $watchHealth.error
      status = $(if ($watchHealth.payload) { $watchHealth.payload.status } else { $null })
      hermes_status = $(if ($watchHealth.payload) { $watchHealth.payload.hermes_status } else { $null })
    }
    hermes_health = [pscustomobject]@{
      ok = $(if ($hermesHealth) { $hermesHealth.ok } else { $null })
      error = $(if ($hermesHealth) { $hermesHealth.error } else { $null })
      status = $(if ($hermesHealth -and $hermesHealth.payload) { $hermesHealth.payload.status } else { $null })
    }
    hermes_models = [pscustomobject]@{
      ok = $(if ($hermesModels) { $hermesModels.ok } else { $null })
      error = $(if ($hermesModels) { $hermesModels.error } else { $null })
      model_count = $modelIds.Count
      first_model = $($modelIds | Select-Object -First 1)
    }
  }
} | ConvertTo-Json -Depth 8
