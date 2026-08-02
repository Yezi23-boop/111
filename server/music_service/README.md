# AI Memory Watch music-service

这是香港 1Panel 主栈中的独立音乐服务。固定测试流用于验证设备鉴权、SQLite
播放会话、单个 FFmpeg 和 `audio/mpeg` 分块输出；生产模式通过进程内
`api-enhanced` 私有适配访问个人网易云账号。

生产接口只保留二维码登录与确认、账户退出二次确认、今日推荐、我喜欢、我的歌单、
最近播放，以及按来源创建播放会话。网易云 Cookie 只写入
`NETEASE_COOKIE_PATH` 指定的私有文件，播放地址只作为服务端 FFmpeg 输入，不下发
给手表。

## 本地测试

Node.js 需要 22.5 或更新版本，因为会使用内置 `node:sqlite`。运行：

```powershell
npm test
```

固定流测试通过 `MUSIC_TEST_MODE=true` 和 `MUSIC_TEST_STREAM_PATH` 指向本地 MP3
文件。生产环境必须保持 `MUSIC_TEST_MODE=false`。二维码有效期为 120 秒，目录
缓存为进程内 5 分钟；登录成功不会自动开始播放。

## 香港部署边界

- Compose 服务名：`ai-memory-watch-music-service`
- 容器监听：`8788`
- 宿主机只绑定：`127.0.0.1:18788`
- 数据目录：`/opt/ai-memory-watch/music-data:/data`
- 公开路径：由 OpenResty 转发的鉴权 `/v1/music/*`
- 不加入 Hermes 或 `watch-relay-private` 网络
- `WATCH_DEVICE_TOKENS` 与 watch endpoint 使用同一配置源，但 music-service 独立校验

不要把 `WATCH_DEVICE_TOKENS`、网易云 Cookie、播放 URL 或 API key 写入仓库、镜像
或日志。音频流不写入 `/data`。
