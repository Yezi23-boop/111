param(
  [string]$TokenFile = "D:\Docker_data\hermes\cloudflared_tunnel_token.txt",
  [string]$ContainerName = "ai-memory-watch-cloudflared",
  [string]$Image = "cloudflare/cloudflared:latest",
  [switch]$ReplaceExisting,
  [switch]$Pull
)

$ErrorActionPreference = "Stop"

function Get-FullPath {
  param([string]$Path)
  return [System.IO.Path]::GetFullPath($Path)
}

function Test-PathInside {
  param(
    [string]$Child,
    [string]$Parent
  )

  $childFull = Get-FullPath -Path $Child
  $parentFull = Get-FullPath -Path $Parent
  if (-not $parentFull.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
    $parentFull += [System.IO.Path]::DirectorySeparatorChar
  }

  return $childFull.StartsWith($parentFull, [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-ContainerStatus {
  param([string]$Name)

  $status = docker ps -a --filter "name=^/$Name$" --format "{{.Status}}"
  if ($LASTEXITCODE -ne 0) {
    throw "docker ps failed"
  }
  if ([string]::IsNullOrWhiteSpace($status)) {
    return $null
  }
  return $status.Trim()
}

$repoRoot = Get-FullPath -Path (Join-Path $PSScriptRoot "..\..\..")
$resolvedTokenFile = Get-FullPath -Path $TokenFile

if (Test-PathInside -Child $resolvedTokenFile -Parent $repoRoot) {
  [pscustomobject]@{
    status = "failed"
    reason = "token_file_inside_repo"
    container_name = $ContainerName
    token_file_present = Test-Path -LiteralPath $resolvedTokenFile
  } | ConvertTo-Json -Depth 5
  exit 1
}

if (-not (Test-Path -LiteralPath $resolvedTokenFile)) {
  [pscustomobject]@{
    status = "failed"
    reason = "token_file_missing"
    container_name = $ContainerName
    token_file_present = $false
    expected_token_file = $resolvedTokenFile
  } | ConvertTo-Json -Depth 5
  exit 1
}

$existingStatus = Get-ContainerStatus -Name $ContainerName
if ($existingStatus -ne $null) {
  if (-not $ReplaceExisting) {
    [pscustomobject]@{
      status = "exists"
      reason = "container_already_exists"
      container_name = $ContainerName
      container_status = $existingStatus
      token_file_present = $true
    } | ConvertTo-Json -Depth 5
    exit 0
  }

  docker rm -f $ContainerName | Out-Null
  if ($LASTEXITCODE -ne 0) {
    throw "docker rm failed"
  }
}

if ($Pull) {
  docker pull $Image | Out-Null
  if ($LASTEXITCODE -ne 0) {
    throw "docker pull failed"
  }
}

$containerTokenPath = "/run/secrets/cloudflared_tunnel_token"
$volume = "${resolvedTokenFile}:$containerTokenPath`:ro"

$containerId = docker run -d `
  --name $ContainerName `
  --restart unless-stopped `
  -e "TUNNEL_TOKEN_FILE=$containerTokenPath" `
  -v $volume `
  $Image `
  tunnel --no-autoupdate run

if ($LASTEXITCODE -ne 0) {
  throw "docker run failed"
}

Start-Sleep -Seconds 3
$startedStatus = Get-ContainerStatus -Name $ContainerName

[pscustomobject]@{
  status = "started"
  container_name = $ContainerName
  container_id = $containerId.Trim()
  container_status = $startedStatus
  token_file_present = $true
  token_file_mode = "mounted_file"
  token_value_printed = $false
} | ConvertTo-Json -Depth 5
