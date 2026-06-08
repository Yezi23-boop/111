param(
  [string]$HermesUrl = "http://127.0.0.1:8642",
  [string]$HermesEnvFile = "D:\Docker_data\hermes\data\.env",
  [string]$Model = "hermes-agent",
  [string]$InputText = "手表用户说：记一下明天看电池日志",
  [string]$Conversation = "watch-001-ai-memory-watch"
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

function New-Failure {
  param(
    [string]$Reason,
    [string]$ErrorMessage = $null
  )

  [pscustomobject]@{
    status = "failed"
    reason = $Reason
    hermes_url = $HermesUrl
    generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
    error = $ErrorMessage
  } | ConvertTo-Json -Depth 4
  exit 1
}

function Get-HermesOutputText {
  param([object]$Payload)

  if ($Payload.output_text) {
    return [string]$Payload.output_text
  }

  $texts = New-Object System.Collections.Generic.List[string]
  foreach ($item in @($Payload.output)) {
    if ($item.type -ne "message") {
      continue
    }
    foreach ($part in @($item.content)) {
      if ($part.text) {
        $texts.Add([string]$part.text)
      }
    }
  }

  return ($texts -join "`n").Trim()
}

$apiKey = Get-EnvValue -Path $HermesEnvFile -Key "API_SERVER_KEY"
if ([string]::IsNullOrWhiteSpace($apiKey)) {
  New-Failure -Reason "api_server_key_missing"
}

$headers = @{ Authorization = "Bearer $apiKey" }

try {
  $health = Invoke-RestMethod -Uri "$HermesUrl/health" -Headers $headers -TimeoutSec 10
} catch {
  New-Failure -Reason "health_unreachable" -ErrorMessage $_.Exception.Message
}

try {
  $models = Invoke-RestMethod -Uri "$HermesUrl/v1/models" -Headers $headers -TimeoutSec 10
} catch {
  New-Failure -Reason "models_unreachable" -ErrorMessage $_.Exception.Message
}

$body = @{
  model = $Model
  input = $InputText
  conversation = $Conversation
}

try {
  $response = Invoke-RestMethod -Uri "$HermesUrl/v1/responses" `
    -Method Post `
    -Headers $headers `
    -ContentType "application/json" `
    -Body ($body | ConvertTo-Json -Depth 4) `
    -TimeoutSec 130
} catch {
  New-Failure -Reason "responses_unreachable" -ErrorMessage $_.Exception.Message
}

$outputText = Get-HermesOutputText -Payload $response
$modelIds = @()
if ($models.data) {
  $modelIds = @($models.data | ForEach-Object { $_.id })
}

if ($health.status -ne "ok") {
  New-Failure -Reason "health_not_ok" -ErrorMessage "Hermes health status: $($health.status)"
}
if ($modelIds.Count -lt 1) {
  New-Failure -Reason "models_empty"
}
if ([string]::IsNullOrWhiteSpace($outputText)) {
  New-Failure -Reason "responses_output_empty"
}

[pscustomobject]@{
  status = "passed"
  hermes_url = $HermesUrl
  generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
  health_status = $health.status
  model_count = $modelIds.Count
  first_model = $($modelIds | Select-Object -First 1)
  response_status = $response.status
  response_id_present = -not [string]::IsNullOrWhiteSpace([string]$response.id)
  output_text_present = $true
  output_text_chars = $outputText.Length
  conversation = $Conversation
} | ConvertTo-Json -Depth 4
