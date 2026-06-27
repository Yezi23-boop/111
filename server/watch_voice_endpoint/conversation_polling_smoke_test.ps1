param(
    [string]$BaseUrl = "http://127.0.0.1:8787",
    [string]$DeviceId = "watch-001",
    [string]$DeviceToken = "",
    [string]$EnvFile = "D:\Docker_data\hermes\watch_voice_endpoint.env",
    [string]$AudioPath = "",
    [string]$MockAsrText = "离页后继续处理",
    [int]$PollTimeoutSeconds = 120
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

function Convert-ToWsUrl {
    param([string]$HttpBaseUrl)
    $trimmed = $HttpBaseUrl.TrimEnd("/")
    if ($trimmed.StartsWith("https://")) {
        return "wss://" + $trimmed.Substring(8) + "/v1/watch/ws"
    }
    if ($trimmed.StartsWith("http://")) {
        return "ws://" + $trimmed.Substring(7) + "/v1/watch/ws"
    }
    if ($trimmed.StartsWith("wss://") -or $trimmed.StartsWith("ws://")) {
        return $trimmed.TrimEnd("/")
    }
    throw "unsupported BaseUrl scheme"
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
    $AudioPath = Join-Path ([System.IO.Path]::GetTempPath()) ("ai-memory-watch-conv-smoke-{0}.ogg" -f ([guid]::NewGuid().ToString("N")))
    [System.IO.File]::WriteAllBytes($AudioPath, [byte[]]@(0x4F, 0x67, 0x67, 0x53, 0x00, 0x01))
    $createdTempAudio = $true
}

if (-not (Test-Path -LiteralPath $AudioPath)) {
    throw "audio file not found: $AudioPath"
}

$requestId = "{0}-conv-smoke-{1}" -f $DeviceId, ([guid]::NewGuid().ToString("N").Substring(0, 12))
$wsUrl = Convert-ToWsUrl -HttpBaseUrl $BaseUrl
$socket = [System.Net.WebSockets.ClientWebSocket]::new()

try {
    $null = $socket.ConnectAsync([Uri]$wsUrl, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
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

    $null = $socket.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "detach", [Threading.CancellationToken]::None).GetAwaiter().GetResult()

    $headers = @{ Authorization = "Bearer $resolvedToken" }
    $deadline = (Get-Date).AddSeconds($PollTimeoutSeconds)
    $messages = @()
    do {
        Start-Sleep -Milliseconds 500
        $uri = "{0}/v1/watch/conversation?device_id={1}" -f $BaseUrl.TrimEnd("/"), [uri]::EscapeDataString($DeviceId)
        $response = Invoke-RestMethod -Method Get -Uri $uri -Headers $headers
        $messages = @($response.messages | Where-Object { $_.request_id -eq $requestId })
        $assistant = @($messages | Where-Object { $_.role -eq "assistant" }) | Select-Object -Last 1
        if ($assistant) {
            break
        }
    } while ((Get-Date) -lt $deadline)

    $assistant = @($messages | Where-Object { $_.role -eq "assistant" }) | Select-Object -Last 1
    if (-not $assistant) {
        throw "conversation polling timed out without assistant reply"
    }

    [ordered]@{
        status = "passed"
        base_url = $BaseUrl
        request_id = $requestId
        user_message_seen = [bool](@($messages | Where-Object { $_.role -eq "user" }) | Select-Object -First 1)
        assistant_status = $assistant.status
        assistant_text_present = [bool]$assistant.text
        assistant_text_chars = if ($assistant.text) { ([string]$assistant.text).Length } else { 0 }
        field_count = ($assistant | Get-Member -MemberType NoteProperty).Count
    } | ConvertTo-Json -Depth 5
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
