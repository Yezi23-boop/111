# 本地 OTA 上位机

该工具仅服务当前独立 HTTPS OTA 协议：发布一个 manifest 和一个应用镜像。它不操作串口，不写入设备 Flash，也不依赖 `official_chat`。

## 准备发布物

将 `192.168.1.20` 换成运行上位机的电脑在设备可访问 Wi-Fi 中的地址：

```powershell
python tools/ota_host/ota_host.py prepare `
  --bin build/111.bin `
  --version 0.1.0 `
  --base-url https://192.168.1.20:8443 `
  --output-dir .ota-release
```

命令会复制镜像，并生成：

```text
.ota-release/
  manifest.json
  111.bin
```

manifest 含 `version`、HTTPS `url`、`size` 和 `sha256`。设备端必须将上位机证书的签发 CA 配置为可信；不要跳过证书校验。

## 启动 HTTPS 服务

证书和私钥由开发 CA、企业 CA 或受控测试证书提供，工具不会自动生成或信任临时证书：

```powershell
python tools/ota_host/ota_host.py serve `
  --directory .ota-release `
  --bind 0.0.0.0 `
  --port 8443 `
  --cert .\certs\ota-server.pem `
  --key .\certs\ota-server-key.pem
```

按 `Ctrl+C` 停止。服务会在标准错误输出每个请求的方法、路径和状态码。

## 故障注入

开发测试时追加 `--fault`：

```text
bad-sha     manifest 返回错误 SHA-256，镜像本身不变
truncated   镜像只发送一半，Content-Length 也是一半
disconnect  镜像声明完整长度，发送一半后断开 TLS 连接
```

只对专用测试设备和测试镜像使用故障模式。
