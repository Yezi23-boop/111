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

## 差分 OTA 板测发布（detools patch）

设备端已支持 delta 路径（`CONFIG_OTA_DELTA_ENABLED` 默认关闭，板测时开启）：
manifest 携带完整 delta 字段则走"流式下载 patch + feed 解压 + 应用后 SHA256
校验"，任一环节失败自动回退 manifest 中的全量字段（须保留），不影响生产发布。

### 1. 服务器新增 patch 静态路由（一次性，服务器代码可改动）

在 `/opt/ai-memory-watch/watch-endpoint/app.py` 中复用现有 artifact 读取模式，
新增（按服务器实际磁盘/volume 路径调整）：

```python
@app.get("/v1/watch/ota/patches/{channel}/{name}")
async def serve_patch(channel: str, name: str):
    """差分 OTA 测试：静态读磁盘 patch 文件，不校验签名，仅板测期使用。"""
    return await serve_artifact(channel, name, kind="patches")  # 或等价现有实现
```

patch 文件放服务器 volume 的 `patches/stable/` 目录（与 artifacts 同级），
重启 `ai-memory-watch-voice-endpoint` 容器后生效。

### 2. 本地生成 patch（需 detools + esptool）

```powershell
# 首次需安装 detools（与 esp_delta_ota v1.1.4 配套版本）
pip install detools==0.49.0
python tools/ota_host/make_delta_patch.py `
  --base build_1.0.7/111.bin `      # 设备当前在跑的旧版本 bin
  --new build/111.bin `             # 目标新版本 bin
  --baseline-version 1.0.7 `
  --version 1.0.8 `
  --channel stable
```

脚本输出：
- `1.0.7_to_1.0.8.patch`（64 字节头：magic + baseline 镜像 SHA256 + 保留字节）
- 本地还原校验结果（必须 OK 才能发布）
- 待填写的 manifest delta 字段 JSON

### 3. 服务器放 patch + 改 manifest

```bash
# 1) 传 patch 到服务器静态目录
scp -i "C:\Users\ye\.ssh\hermes.pem" 1.0.7_to_1.0.8.patch \
    root@8.134.203.76:/opt/ai-memory-watch/watch-endpoint/<volume>/patches/stable/

# 2) 手动编辑 releases/stable/manifest.json，
#    在保留 url/size/sha256 全量字段的同时，加入脚本输出的 delta 字段：
#    baseline_version / patch_url / patch_size / patch_sha256 / target_sha256
```

> 注意：`baseline_version` 必须等于设备当前 `esp_app_get_description()->version`，
> 否则设备端拒绝并回退全量；`target_sha256` 必须等于全量字段 `sha256`。

### 4. 真机板测

1. 构建开启 delta 的固件：`menuconfig` 打开
   `CONFIG_OTA_DELTA_ENABLED`，`CONFIG_OTA_DELTA_MAX_RETRY=3`，
   `CONFIG_OTA_METRICS_LOG_COUNT=10`，`idf.py build`；
2. 板端触发 OTA，串口观察（弱网可限速复现 26KB/s 场景）：
   - `ota_metrics` 模块在启动时打印最近 N 次升级结果
     （`[ota_metrics] recent OTA results`）；
   - 每阶段结束打印 `record stage=manifest/download/activate ... delta=1`；
   - delta 失败回退时出现 `delta exhausted, falling back to full image`；
3. 升级成功后确认：应用跑新版本、`ota_boot_check` 标记 valid、下次启动
   dump 显示 manifest/download/activate 三条 delta=1 且 result=ESP_OK；
4. 弱网中断复测：确认重试次数 ≤ `CONFIG_OTA_DELTA_MAX_RETRY`，最终回退
   全量仍能成功，双槽回滚机制照常生效。

### 5. 测试收尾（必须完成）

板测通过后，务必删除临时数据，避免污染生产：
1. 从服务器 `releases/stable/manifest.json` 删除
   `baseline_version/patch_url/patch_size/patch_sha256/target_sha256` 五个字段，
   恢复纯全量 manifest；
2. 删除服务器 `patches/stable/*.patch` 测试文件；
3. （可选）确认删除后设备端再次拉 manifest 走全量路径、行为无回归。
