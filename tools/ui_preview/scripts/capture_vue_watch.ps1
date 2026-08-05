param(
    [string]$Url = "http://127.0.0.1:8767/",
    [string]$Selector = ".watch",
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [string]$SetupScriptPath,
    [ValidatePattern("^[A-Za-z0-9_-]+$")]
    [string]$SessionName = "vue-lvgl-capture"
)

$ErrorActionPreference = "Stop"

function Invoke-PlaywrightCommand {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)

    & $script:PlaywrightCli "-s=$SessionName" @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "playwright-cli failed with exit code ${LASTEXITCODE}: $($Arguments -join ' ')"
    }
}

try {
    Invoke-WebRequest -Uri $Url -UseBasicParsing -TimeoutSec 5 | Out-Null
} catch {
    throw "Vue preview is not reachable at $Url. Start it with: cd tools/ui_prototypes/watch-vue; npm run dev"
}

$command = Get-Command "playwright-cli" -ErrorAction SilentlyContinue
if ($null -eq $command) {
    throw "playwright-cli is not available. Install or expose it before capturing Vue screenshots."
}
$script:PlaywrightCli = $command.Source

$resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $resolvedOutput
if (-not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
}

if ($SetupScriptPath) {
    $resolvedSetup = (Resolve-Path -LiteralPath $SetupScriptPath).Path
}

# Clear a stale named session. A missing session is harmless.
& $script:PlaywrightCli "-s=$SessionName" close 2>$null | Out-Null

try {
    Invoke-PlaywrightCommand open $Url | Out-Null
    Invoke-PlaywrightCommand resize 1280 720 | Out-Null

    if ($resolvedSetup) {
        Invoke-PlaywrightCommand run-code "--filename=$resolvedSetup" | Out-Null
    }

    $selectorJson = ConvertTo-Json $Selector -Compress
    $prepareCode = @"
async page => {
  const selector = $selectorJson;
  await page.locator(selector).waitFor({ state: 'visible' });
  await page.evaluate(async () => {
    if (document.fonts) await document.fonts.ready;
    for (const animation of document.getAnimations()) animation.finish();
  });
}
"@
    Invoke-PlaywrightCommand run-code $prepareCode | Out-Null
    Invoke-PlaywrightCommand screenshot $Selector "--filename=$resolvedOutput" | Out-Null
} finally {
    & $script:PlaywrightCli "-s=$SessionName" close 2>$null | Out-Null
}

if (-not (Test-Path -LiteralPath $resolvedOutput)) {
    throw "Vue capture did not create output file: $resolvedOutput"
}

Write-Output $resolvedOutput
