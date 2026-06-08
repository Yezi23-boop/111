param(
  [string]$BaseUrl = "http://127.0.0.1:8787",
  [string]$EnvFile = "D:\Docker_data\hermes\watch_voice_endpoint.env",
  [string]$HermesUrl = "http://127.0.0.1:8642",
  [string]$HermesEnvFile = "D:\Docker_data\hermes\data\.env",
  [string]$DeviceId = "watch-001",
  [string]$SampleText = "记一下明天看电池日志",
  [switch]$RebuildContainer,
  [switch]$SkipAcceptance,
  [switch]$SkipDocker,
  [switch]$SkipHermesApi,
  [switch]$SkipServiceHealth,
  [switch]$SkipRealAsr
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptRoot "..\..")
$requirements = Join-Path $scriptRoot "requirements.txt"
$testsDir = Join-Path $scriptRoot "tests"
$acceptanceScript = Join-Path $scriptRoot "acceptance_test.ps1"
$composeFile = Join-Path $scriptRoot "compose.local.yml"

function Convert-JsonOutput {
  param(
    [Parameter(Mandatory = $true)]
    [object[]]$Output
  )

  return ($Output | Out-String | ConvertFrom-Json)
}

function Get-OutputTail {
  param(
    [object[]]$Output,
    [int]$MaxLines = 12
  )

  if ($null -eq $Output) {
    return @()
  }
  return @($Output | Select-Object -Last $MaxLines | ForEach-Object { "$_" })
}

$pytestOutput = $null
Push-Location $repoRoot
try {
  $pytestOutput = & uv run --with-requirements $requirements python -m pytest $testsDir -q 2>&1
  $pytestExitCode = $LASTEXITCODE
} finally {
  Pop-Location
}

$rebuildSummary = [pscustomobject]@{
  requested = [bool]$RebuildContainer
  exit_code = $null
  output_tail = @()
}
if ($RebuildContainer) {
  Push-Location $scriptRoot
  try {
    $rebuildOutput = & docker compose -f $composeFile up -d --build 2>&1
    $rebuildSummary.exit_code = $LASTEXITCODE
    $rebuildSummary.output_tail = Get-OutputTail -Output $rebuildOutput
  } finally {
    Pop-Location
  }
}

$acceptance = $null
$acceptanceExitCode = $null
$acceptanceParseError = $null
if (-not $SkipAcceptance -and $pytestExitCode -eq 0 -and ($rebuildSummary.exit_code -eq $null -or $rebuildSummary.exit_code -eq 0)) {
  $acceptanceArgs = @{
    BaseUrl = $BaseUrl
    EnvFile = $EnvFile
    HermesUrl = $HermesUrl
    HermesEnvFile = $HermesEnvFile
    DeviceId = $DeviceId
    SampleText = $SampleText
  }
  if ($SkipDocker) {
    $acceptanceArgs.SkipDocker = $true
  }
  if ($SkipHermesApi) {
    $acceptanceArgs.SkipHermesApi = $true
  }
  if ($SkipServiceHealth) {
    $acceptanceArgs.SkipServiceHealth = $true
  }
  if ($SkipRealAsr) {
    $acceptanceArgs.SkipRealAsr = $true
  }

  $acceptanceOutput = & $acceptanceScript @acceptanceArgs 2>&1
  $acceptanceExitCode = $LASTEXITCODE
  try {
    $acceptance = Convert-JsonOutput -Output $acceptanceOutput
  } catch {
    $acceptanceParseError = $_.Exception.Message
  }
}

$status = "passed"
$reason = $null
if ($pytestExitCode -ne 0) {
  $status = "failed"
  $reason = "pytest_failed"
} elseif ($rebuildSummary.exit_code -ne $null -and $rebuildSummary.exit_code -ne 0) {
  $status = "failed"
  $reason = "container_rebuild_failed"
} elseif ($acceptanceExitCode -ne $null -and $acceptanceExitCode -ne 0) {
  $status = "failed"
  $reason = "acceptance_failed"
} elseif ($acceptanceParseError) {
  $status = "failed"
  $reason = "acceptance_json_parse_failed"
}

$summary = [pscustomobject]@{
  status = $status
  reason = $reason
  generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
  base_url = $BaseUrl
  device_id = $DeviceId
  pytest = [pscustomobject]@{
    exit_code = $pytestExitCode
    output_tail = Get-OutputTail -Output $pytestOutput
  }
  container_rebuild = $rebuildSummary
  acceptance = $(if ($acceptance) {
      [pscustomobject]@{
        exit_code = $acceptanceExitCode
        status = $acceptance.status
        runtime_before = $acceptance.runtime_before
        hermes_text_smoke = $acceptance.hermes_text_smoke
        mock_smoke = $acceptance.mock_smoke
        real_asr_smoke = $acceptance.real_asr_smoke
        runtime_after = $acceptance.runtime_after
      }
    } else {
      [pscustomobject]@{
        skipped = [bool]$SkipAcceptance
        exit_code = $acceptanceExitCode
        parse_error = $acceptanceParseError
      }
    })
}

$summary | ConvertTo-Json -Depth 10
if ($status -ne "passed") {
  exit 1
}
