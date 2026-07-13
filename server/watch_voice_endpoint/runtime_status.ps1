param(
  [string]$BaseUrl = "http://127.0.0.1:8787",
  [string]$HermesUrl = "http://127.0.0.1:8642",
  [string]$WatchEnvFile = "D:\Docker_data\hermes\watch_voice_endpoint.env",
  [string]$HermesEnvFile = "D:\Docker_data\hermes\data\.env",
  [string]$DeviceId = "watch-001",
  [switch]$SkipDocker,
  [switch]$SkipHermesApi,
  [switch]$SkipServiceHealth,
  [switch]$AssertPrivateNotExposed
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

function Invoke-PrivateExposureCheck {
  param(
    [string]$Uri,
    [string]$Path,
    [ValidateSet("GET", "POST")]
    [string]$Method = "GET",
    [int[]]$AllowedStatusCodes = @(403, 404, 410),
    [int]$TimeoutSec = 10
  )

  try {
    $response = Invoke-WebRequest -Uri $Uri -Method $Method -TimeoutSec $TimeoutSec -SkipHttpErrorCheck
    $statusCode = [int]$response.StatusCode
    $isAllowed = $AllowedStatusCodes -contains $statusCode
    return [pscustomobject]@{
      path = $Path
      ok = $isAllowed
      exposed = -not $isAllowed
      status_code = $statusCode
      method = $Method
      allowed_status_codes = $AllowedStatusCodes
      error = $null
    }
  } catch {
    return [pscustomobject]@{
      path = $Path
      ok = $false
      exposed = $false
      status_code = $null
      method = $Method
      allowed_status_codes = $AllowedStatusCodes
      error = $_.Exception.Message
    }
  }
}

function Get-ContainerStatus {
  param([string]$Name)

  $inspectOutput = docker inspect $Name 2>&1
  if ($LASTEXITCODE -ne 0) {
    $message = ($inspectOutput | Out-String).Trim()
    if ($message -match "No such object") {
      return [pscustomobject]@{
        exists = $false
        status = "missing"
        health = $null
        error = $null
      }
    }

    return [pscustomobject]@{
      exists = $null
      status = "inspect_unavailable"
      health = $null
      error = $message
    }
  }

  try {
    $json = $inspectOutput | ConvertFrom-Json
    $container = $json[0]
    $health = $null
    if ($container.State.Health) {
      $health = $container.State.Health.Status
    }
    return [pscustomobject]@{
      exists = $true
      status = $container.State.Status
      health = $health
      error = $null
    }
  } catch {
    return [pscustomobject]@{
      exists = $null
      status = "inspect_parse_error"
      health = $null
      error = $_.Exception.Message
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

$serviceHealth = $null
if (-not $SkipServiceHealth) {
  $serviceHealth = Invoke-StatusGet -Uri "$BaseUrl/health" -TimeoutSec 5
}
$watchHealth = Invoke-StatusGet -Uri "$BaseUrl/v1/watch/health?device_id=$DeviceId" -Headers $watchHeaders -TimeoutSec 10

$privateExposureChecks = @()
if ($AssertPrivateNotExposed) {
  $privateBaseUrl = $BaseUrl.TrimEnd("/")
  $privateExposureChecks = @(
    Invoke-PrivateExposureCheck -Uri "$privateBaseUrl/health" -Path "/health" -TimeoutSec 10
    Invoke-PrivateExposureCheck -Uri "$privateBaseUrl/v1/models" -Path "/v1/models" -TimeoutSec 10
    Invoke-PrivateExposureCheck -Uri "$privateBaseUrl/v1/responses" -Path "/v1/responses" -TimeoutSec 10
    Invoke-PrivateExposureCheck -Uri "$privateBaseUrl/internal/watch/inbox" -Path "/internal/watch/inbox" -Method POST -AllowedStatusCodes @(404, 410) -TimeoutSec 10
  )
}
$privateExposureOk = $null
if ($AssertPrivateNotExposed) {
  $privateExposureOk = -not [bool](
    $privateExposureChecks | Where-Object { -not $_.ok } | Select-Object -First 1
  )
}

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
      skipped = [bool]$SkipServiceHealth
      ok = $(if ($serviceHealth) { $serviceHealth.ok } else { $null })
      error = $(if ($serviceHealth) { $serviceHealth.error } else { $null })
      status = $(if ($serviceHealth -and $serviceHealth.payload) { $serviceHealth.payload.status } else { $null })
      asr_provider = $(if ($serviceHealth -and $serviceHealth.payload) { $serviceHealth.payload.asr_provider } else { $null })
      request_timeout_seconds = $(if ($serviceHealth -and $serviceHealth.payload) { $serviceHealth.payload.request_timeout_seconds } else { $null })
      inflight_requests = $(if ($serviceHealth -and $serviceHealth.payload) { $serviceHealth.payload.inflight_requests } else { $null })
      request_events = $(if ($serviceHealth -and $serviceHealth.payload) { $serviceHealth.payload.request_events } else { $null })
      request_status_counts = $(if ($serviceHealth -and $serviceHealth.payload) { $serviceHealth.payload.request_status_counts } else { $null })
      ws_request_status_counts = $(if ($serviceHealth -and $serviceHealth.payload) { $serviceHealth.payload.ws_request_status_counts } else { $null })
      auth_failures = $(if ($serviceHealth -and $serviceHealth.payload) { $serviceHealth.payload.auth_failures } else { $null })
      request_error_counts = $(if ($serviceHealth -and $serviceHealth.payload) { $serviceHealth.payload.request_error_counts } else { $null })
      last_request = $(if ($serviceHealth -and $serviceHealth.payload) { $serviceHealth.payload.last_request } else { $null })
      last_ws_request = $(if ($serviceHealth -and $serviceHealth.payload) { $serviceHealth.payload.last_ws_request } else { $null })
      last_auth_failure = $(if ($serviceHealth -and $serviceHealth.payload) { $serviceHealth.payload.last_auth_failure } else { $null })
    }
    private_exposure = [pscustomobject]@{
      checked = [bool]$AssertPrivateNotExposed
      ok = $privateExposureOk
      checks = $privateExposureChecks
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
