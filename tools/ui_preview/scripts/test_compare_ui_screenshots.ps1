$ErrorActionPreference = "Stop"

Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase

function Write-TestPng {
    param(
        [string]$Path,
        [byte[]]$Pixels
    )

    $bitmap = [System.Windows.Media.Imaging.BitmapSource]::Create(
        2,
        2,
        96,
        96,
        [System.Windows.Media.PixelFormats]::Bgra32,
        $null,
        $Pixels,
        8
    )
    $encoder = [System.Windows.Media.Imaging.PngBitmapEncoder]::new()
    $encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
    $stream = [System.IO.File]::Create($Path)
    try {
        $encoder.Save($stream)
    } finally {
        $stream.Dispose()
    }
}

$testRoot = Join-Path $env:TEMP "vue-lvgl-pixel-ui-self-test-$PID"
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$referencePath = Join-Path $testRoot "reference.png"
$actualPath = Join-Path $testRoot "actual.png"
$passOutput = Join-Path $testRoot "pass"
$failOutput = Join-Path $testRoot "fail"
$cropOutput = Join-Path $testRoot "crop"
$compareScript = Join-Path $PSScriptRoot "compare_ui_screenshots.ps1"

try {
    $referencePixels = [byte[]](0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255)
    $actualPixels = [byte[]](255, 255, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255)
    Write-TestPng -Path $referencePath -Pixels $referencePixels
    Write-TestPng -Path $actualPath -Pixels $actualPixels

    & $compareScript -ReferencePath $referencePath -ActualPath $referencePath -OutputDirectory $passOutput -MaxMismatchRatio 0 | Out-Null
    $passReport = Get-Content -Raw (Join-Path $passOutput "full-page-report.json") | ConvertFrom-Json
    if (-not $passReport.passed -or $passReport.mismatch_pixels -ne 0) {
        throw "Identical-image comparison did not pass."
    }

    $failedAsExpected = $false
    try {
        & $compareScript -ReferencePath $referencePath -ActualPath $actualPath -OutputDirectory $failOutput -MaxMismatchRatio 0 | Out-Null
    } catch {
        $failedAsExpected = $true
    }
    $failReport = Get-Content -Raw (Join-Path $failOutput "full-page-report.json") | ConvertFrom-Json
    if (-not $failedAsExpected -or $failReport.passed -or $failReport.mismatch_pixels -ne 1) {
        throw "Different-image comparison did not report exactly one mismatched pixel."
    }

    & $compareScript -ReferencePath $referencePath -ActualPath $actualPath -OutputDirectory $cropOutput -LayerName "unchanged-row" -Crop 0,1,2,1 -MaxMismatchRatio 0 | Out-Null
    Write-Output "UI screenshot comparison self-test passed."
} finally {
    $generatedFiles = @(
        $referencePath,
        $actualPath,
        (Join-Path $passOutput "full-page-report.json"),
        (Join-Path $passOutput "full-page-report.md"),
        (Join-Path $passOutput "full-page-diff.png"),
        (Join-Path $failOutput "full-page-report.json"),
        (Join-Path $failOutput "full-page-report.md"),
        (Join-Path $failOutput "full-page-diff.png"),
        (Join-Path $cropOutput "unchanged-row-report.json"),
        (Join-Path $cropOutput "unchanged-row-report.md"),
        (Join-Path $cropOutput "unchanged-row-diff.png")
    )
    foreach ($path in $generatedFiles) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Force
        }
    }
    foreach ($directory in @($passOutput, $failOutput, $cropOutput, $testRoot)) {
        if (Test-Path -LiteralPath $directory) {
            Remove-Item -LiteralPath $directory -Force
        }
    }
}
