# 运行时资源

此目录会被打包成 LittleFS 镜像，烧录到 `resources` 分区，并在启动时挂载到
`/resources`。

建议布局：

- `fonts/`：存放 LVGL 原生 binfont，通过 `A:/fonts/name.bin` 访问。
- `audio/`：存放较小的可替换音频片段。
- `images/`：存放 LVGL 文件图片或其他 UI 资源。
