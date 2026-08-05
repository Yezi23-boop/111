param(
    [Parameter(Mandatory = $true)]
    [string]$ReferencePath,
    [Parameter(Mandatory = $true)]
    [string]$ActualPath,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [string]$LayerName = "full-page",
    [ValidateRange(0, 255)]
    [int]$ChannelTolerance = 8,
    [ValidateRange(0.0, 1.0)]
    [double]$MaxMismatchRatio = 0.005,
    [int[]]$Crop
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase

function Read-BgraImage {
    param([string]$Path)

    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    $stream = [System.IO.File]::OpenRead($resolvedPath)
    try {
        $decoder = [System.Windows.Media.Imaging.PngBitmapDecoder]::new(
            $stream,
            [System.Windows.Media.Imaging.BitmapCreateOptions]::PreservePixelFormat,
            [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad
        )
        $source = $decoder.Frames[0]
        if ($source.Format -ne [System.Windows.Media.PixelFormats]::Bgra32) {
            $source = [System.Windows.Media.Imaging.FormatConvertedBitmap]::new(
                $source,
                [System.Windows.Media.PixelFormats]::Bgra32,
                $null,
                0
            )
        }

        $stride = $source.PixelWidth * 4
        $pixels = New-Object byte[] ($stride * $source.PixelHeight)
        $source.CopyPixels($pixels, $stride, 0)
        return [PSCustomObject]@{
            Path = $resolvedPath
            Width = $source.PixelWidth
            Height = $source.PixelHeight
            Stride = $stride
            Pixels = $pixels
        }
    } finally {
        $stream.Dispose()
    }
}

function Write-BgraPng {
    param(
        [byte[]]$Pixels,
        [int]$Width,
        [int]$Height,
        [string]$Path
    )

    $stride = $Width * 4
    $bitmap = [System.Windows.Media.Imaging.BitmapSource]::Create(
        $Width,
        $Height,
        96,
        96,
        [System.Windows.Media.PixelFormats]::Bgra32,
        $null,
        $Pixels,
        $stride
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

$reference = Read-BgraImage $ReferencePath
$actual = Read-BgraImage $ActualPath
if ($reference.Width -ne $actual.Width -or $reference.Height -ne $actual.Height) {
    throw "Image dimensions differ: reference=$($reference.Width)x$($reference.Height), actual=$($actual.Width)x$($actual.Height)"
}

$cropX = 0
$cropY = 0
$compareWidth = $reference.Width
$compareHeight = $reference.Height
if ($Crop) {
    if ($Crop.Count -ne 4) {
        throw "Crop must contain x,y,width,height."
    }
    $cropX, $cropY, $compareWidth, $compareHeight = $Crop
    if ($cropX -lt 0 -or $cropY -lt 0 -or $compareWidth -le 0 -or $compareHeight -le 0 -or
        $cropX + $compareWidth -gt $reference.Width -or $cropY + $compareHeight -gt $reference.Height) {
        throw "Crop is outside the $($reference.Width)x$($reference.Height) image."
    }
}

$safeLayerName = $LayerName -replace '[^A-Za-z0-9._-]', '-'
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $resolvedOutput)) {
    New-Item -ItemType Directory -Path $resolvedOutput -Force | Out-Null
}

$diffPixels = New-Object byte[] ($compareWidth * $compareHeight * 4)
$mismatchCount = 0L
$absoluteError = 0L
$maxChannelDifference = 0

for ($y = 0; $y -lt $compareHeight; $y++) {
    for ($x = 0; $x -lt $compareWidth; $x++) {
        $sourceIndex = (($cropY + $y) * $reference.Stride) + (($cropX + $x) * 4)
        $outputIndex = (($y * $compareWidth) + $x) * 4

        $blueDifference = [Math]::Abs([int]$reference.Pixels[$sourceIndex] - [int]$actual.Pixels[$sourceIndex])
        $greenDifference = [Math]::Abs([int]$reference.Pixels[$sourceIndex + 1] - [int]$actual.Pixels[$sourceIndex + 1])
        $redDifference = [Math]::Abs([int]$reference.Pixels[$sourceIndex + 2] - [int]$actual.Pixels[$sourceIndex + 2])
        $pixelDifference = [Math]::Max($blueDifference, [Math]::Max($greenDifference, $redDifference))

        $absoluteError += $blueDifference + $greenDifference + $redDifference
        $maxChannelDifference = [Math]::Max($maxChannelDifference, $pixelDifference)
        if ($pixelDifference -gt $ChannelTolerance) {
            $mismatchCount++
            $diffPixels[$outputIndex + 2] = [byte][Math]::Min(255, [Math]::Max(64, $pixelDifference * 4))
        }
        $diffPixels[$outputIndex + 3] = 255
    }
}

$pixelCount = [long]$compareWidth * $compareHeight
$mismatchRatio = if ($pixelCount -eq 0) { 0.0 } else { $mismatchCount / $pixelCount }
$meanAbsoluteError = if ($pixelCount -eq 0) { 0.0 } else { $absoluteError / ($pixelCount * 3.0) }
$passed = $mismatchRatio -le $MaxMismatchRatio

$diffPath = Join-Path $resolvedOutput "$safeLayerName-diff.png"
$jsonPath = Join-Path $resolvedOutput "$safeLayerName-report.json"
$markdownPath = Join-Path $resolvedOutput "$safeLayerName-report.md"
Write-BgraPng -Pixels $diffPixels -Width $compareWidth -Height $compareHeight -Path $diffPath

$report = [ordered]@{
    layer = $LayerName
    passed = $passed
    reference = $reference.Path
    actual = $actual.Path
    canvas = "$($reference.Width)x$($reference.Height)"
    crop = [ordered]@{
        x = $cropX
        y = $cropY
        width = $compareWidth
        height = $compareHeight
    }
    channel_tolerance = $ChannelTolerance
    max_mismatch_ratio = $MaxMismatchRatio
    mismatch_pixels = $mismatchCount
    compared_pixels = $pixelCount
    mismatch_ratio = $mismatchRatio
    mean_absolute_error = $meanAbsoluteError
    max_channel_difference = $maxChannelDifference
    diff_image = $diffPath
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $jsonPath -Encoding utf8

$status = if ($passed) { "PASS" } else { "FAIL" }
$markdown = @"
# UI Comparison: $LayerName

- Status: **$status**
- Reference: $($reference.Path)
- Actual: $($actual.Path)
- Canvas: $($reference.Width)x$($reference.Height)
- Crop: $cropX,$cropY,$compareWidth,$compareHeight
- Channel tolerance: $ChannelTolerance
- Mismatch pixels: $mismatchCount / $pixelCount
- Mismatch ratio: $([Math]::Round($mismatchRatio * 100, 4))%
- Allowed ratio: $([Math]::Round($MaxMismatchRatio * 100, 4))%
- Mean absolute error: $([Math]::Round($meanAbsoluteError, 4))
- Maximum channel difference: $maxChannelDifference
- Diff image: $diffPath
"@
$markdown | Set-Content -LiteralPath $markdownPath -Encoding utf8

Write-Output $jsonPath
Write-Output $markdownPath
Write-Output $diffPath

if (-not $passed) {
    throw "UI comparison failed for '$LayerName': mismatch ratio $([Math]::Round($mismatchRatio * 100, 4))% exceeds $([Math]::Round($MaxMismatchRatio * 100, 4))%."
}
