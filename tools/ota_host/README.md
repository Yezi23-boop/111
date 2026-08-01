# 云端 OTA 发布 CLI

设备端只接受云端 HTTPS manifest 和 artifact；本工具只负责把固件发布到
`watch.934000.xyz` 的云端 OTA 管理 API，不操作串口、不写设备 Flash。

## 发布到云端

如果云端已部署 `watch_voice_endpoint`，可以使用同一个本机工具上传到云端：

```powershell
$env:WATCH_OTA_ADMIN_TOKEN = "<ota-admin-token>"
python tools/ota_host/ota_host.py publish-remote `
  --bin build/111.bin `
  --version 0.2.0 `
  --channel stable
```

默认地址为
`https://watch.934000.xyz/v1/watch/ota/admin/releases`；也可以通过
`--endpoint` 指定其他 HTTPS 云端。网页入口是
`https://watch.934000.xyz/v1/watch/ota/admin`。管理员令牌只用于发布，设备端不保存它。

设备端的弱网断线续传和 SHA-256 校验仍由正式 OTA service 负责。

每次故障后必须用 `otatool.py read_otadata` 和启动日志确认仍从旧槽启动；
看到 `fault_window: finish_succeeded_before_restart` 时可在保持窗口内复位，
验证 `finish()` 已切换选择但尚未调用 `esp_restart()` 的边界。新镜像首次启动
会先经过早期本地 boot-check，再标记 valid；在该标记前复位应走 IDF rollback。
