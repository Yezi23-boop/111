param(
    [string]$BaseUrl = "ws://127.0.0.1:8787/v1/watch/ws",
    [string]$DeviceId = "watch-001",
    [string]$DeviceToken = "",
    [string]$EnvFile = "D:\Docker_data\hermes\watch_voice_endpoint.env",
    [string]$AudioPath = "",
    [string]$MockAsrText = "帮我分析电池日志"
)

$ErrorActionPreference = "Stop"

function Read-EnvValue {
    param([string]$Path, [string]$Name)
    if (-not (Test-Path -LiteralPath $Path)) {
        return ""
    }
    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#")) {
            continue
        }
        $prefix = "$Name="
        if ($trimmed.StartsWith($prefix)) {
            return $trimmed.Substring($prefix.Length).Trim('"')
        }
    }
    return ""
}

function Resolve-DeviceToken {
    param([string]$Explicit, [string]$Path, [string]$Id)
    if ($Explicit) {
        return $Explicit
    }
    $pairs = Read-EnvValue -Path $Path -Name "WATCH_DEVICE_TOKENS"
    foreach ($item in $pairs.Split(",")) {
        $parts = $item.Split("=", 2)
        if ($parts.Count -eq 2 -and $parts[0].Trim() -eq $Id) {
            return $parts[1].Trim()
        }
    }
    return ""
}

function Send-TextFrame {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [string]$Text
    )
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    $segment = [ArraySegment[byte]]::new($bytes)
    $null = $Socket.SendAsync($segment, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
}

function Send-BinaryFrame {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [byte[]]$Bytes
    )
    $segment = [ArraySegment[byte]]::new($Bytes)
    $null = $Socket.SendAsync($segment, [System.Net.WebSockets.WebSocketMessageType]::Binary, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
}

function Receive-JsonFrame {
    param([System.Net.WebSockets.ClientWebSocket]$Socket)
    $buffer = [byte[]]::new(65536)
    $stream = [System.IO.MemoryStream]::new()
    do {
        $segment = [ArraySegment[byte]]::new($buffer)
        $result = $Socket.ReceiveAsync($segment, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        if ($result.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) {
            throw "websocket closed by server"
        }
        $stream.Write($buffer, 0, $result.Count)
    } while (-not $result.EndOfMessage)
    $text = [System.Text.Encoding]::UTF8.GetString($stream.ToArray())
    return $text | ConvertFrom-Json
}

$resolvedToken = Resolve-DeviceToken -Explicit $DeviceToken -Path $EnvFile -Id $DeviceId
if (-not $resolvedToken) {
    throw "missing device token; pass -DeviceToken or configure WATCH_DEVICE_TOKENS in env file"
}

$createdTempAudio = $false
if (-not $AudioPath) {
    $AudioPath = Join-Path ([System.IO.Path]::GetTempPath()) ("ai-memory-watch-ws-smoke-{0}.ogg" -f ([guid]::NewGuid().ToString("N")))
    [System.IO.File]::WriteAllBytes($AudioPath, [byte[]]@(0x4F, 0x67, 0x67, 0x53, 0x00, 0x01))
    $createdTempAudio = $true
}

if (-not (Test-Path -LiteralPath $AudioPath)) {
    throw "audio file not found: $AudioPath"
}

$requestId = "{0}-ws-smoke-{1}" -f $DeviceId, ([guid]::NewGuid().ToString("N").Substring(0, 12))
$socket = [System.Net.WebSockets.ClientWebSocket]::new()
$uri = [Uri]$BaseUrl

try {
    $null = $socket.ConnectAsync($uri, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    Send-TextFrame -Socket $socket -Text (@{
        type = "auth"
        device_id = $DeviceId
        device_token = $resolvedToken
    } | ConvertTo-Json -Compress)

    $auth = Receive-JsonFrame -Socket $socket
    if ($auth.type -ne "auth_ok") {
        throw "auth failed: $($auth.type)/$($auth.error_code)"
    }
    $snapshot = Receive-JsonFrame -Socket $socket
    if ($snapshot.type -ne "conversation_snapshot") {
        throw "expected conversation_snapshot, got $($snapshot.type)"
    }

    Send-TextFrame -Socket $socket -Text (@{
        type = "audio_start"
        request_id = $requestId
        format = "ogg_opus"
        mock_asr_text = $MockAsrText
    } | ConvertTo-Json -Compress)
    $started = Receive-JsonFrame -Socket $socket
    if ($started.type -ne "audio_started") {
        throw "expected audio_started, got $($started.type)"
    }

    [byte[]]$audioBytes = [System.IO.File]::ReadAllBytes($AudioPath)
    Send-BinaryFrame -Socket $socket -Bytes $audioBytes
    Send-TextFrame -Socket $socket -Text (@{
        type = "audio_end"
        request_id = $requestId
    } | ConvertTo-Json -Compress)

    $asr = Receive-JsonFrame -Socket $socket
    $task = Receive-JsonFrame -Socket $socket
    $reply = Receive-JsonFrame -Socket $socket

    $summary = [ordered]@{
        status = "passed"
        base_url = $BaseUrl
        request_id = $requestId
        snapshot_count = @($snapshot.messages).Count
        asr_type = $asr.type
        asr_text_present = [bool]$asr.text
        asr_text_chars = if ($asr.text) { [string]$asr.text | ForEach-Object { $_.Length } } else { 0 }
        task_type = $task.type
        reply_type = $reply.type
        reply_role = $reply.role
        reply_status = $reply.status
        reply_text_present = [bool]$reply.text
        reply_text_chars = if ($reply.text) { [string]$reply.text | ForEach-Object { $_.Length } } else { 0 }
    }
    $json = $summary | ConvertTo-Json -Depth 5
    Write-Output $json
}
finally {
    if ($socket.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
        $null = $socket.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "done", [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    }
    $socket.Dispose()
    if ($createdTempAudio -and (Test-Path -LiteralPath $AudioPath)) {
        Remove-Item -LiteralPath $AudioPath -Force
    }
}
