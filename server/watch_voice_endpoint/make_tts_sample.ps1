param(
  [string]$Text = "记一下明天看电池日志",
  [string]$OutputPath = "",
  [string]$VoiceName = "Microsoft Huihui Desktop"
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
  throw "ffmpeg is required to create an Ogg Opus sample"
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
  $OutputPath = Join-Path $env:TEMP "ai-memory-watch-tts.ogg"
}

$resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)
$outputDir = Split-Path -Parent $resolvedOutput
if (-not (Test-Path -LiteralPath $outputDir)) {
  New-Item -ItemType Directory -Path $outputDir | Out-Null
}

$wavPath = [System.IO.Path]::ChangeExtension($resolvedOutput, ".wav")

Add-Type -AssemblyName System.Speech
$synth = [System.Speech.Synthesis.SpeechSynthesizer]::new()
try {
  $installedVoiceNames = @($synth.GetInstalledVoices() | ForEach-Object { $_.VoiceInfo.Name })
  if ($installedVoiceNames -contains $VoiceName) {
    $synth.SelectVoice($VoiceName)
  } else {
    $zhVoice = $synth.GetInstalledVoices() |
      Where-Object { $_.VoiceInfo.Culture.Name -eq "zh-CN" } |
      Select-Object -First 1
    if ($null -eq $zhVoice) {
      throw "No zh-CN System.Speech voice is installed"
    }
    $VoiceName = $zhVoice.VoiceInfo.Name
    $synth.SelectVoice($VoiceName)
  }

  $synth.SetOutputToWaveFile($wavPath)
  $synth.Speak($Text)
} finally {
  $synth.Dispose()
}

ffmpeg -y -hide_banner -loglevel error -i $wavPath -c:a libopus -b:a 24k $resolvedOutput

$ogg = Get-Item -LiteralPath $resolvedOutput
$wav = Get-Item -LiteralPath $wavPath
[pscustomobject]@{
  text = $Text
  voice = $VoiceName
  wav_path = $wav.FullName
  wav_bytes = $wav.Length
  ogg_path = $ogg.FullName
  ogg_bytes = $ogg.Length
} | ConvertTo-Json -Depth 4
