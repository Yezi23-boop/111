---
id: 2026-08-05-attempt-rgb565-swapped-route-b
summary: 在 LVGL 9.5 正常基线上用 A/B/C 三组真机对照重新验证 RGB565_SWAPPED 全链路路线，B 组显示一切正常，证明历史“SWAPPED 更差”是 9.3 时期双变量叠加的观察，仓库已正式切回路线 B。
tags: [lvgl, display, rgb565, swapped, co5300, byte-order, display-tuning]
owners: components/lvgl_port/lv_port_display.c, components/lvgl_port/lv_port_config.h
triggers: RGB565_SWAPPED, lv_draw_sw_rgb565_swap, byte swap, 颜色花屏, 显示颜色错乱, CONFIG_LV_DRAW_SW_SUPPORT_RGB565_SWAPPED
record_reasons: 证伪历史“切 RGB565_SWAPPED 后显示更差”的结论；包含三组真机对照证据与最终切换决定，后续排查不应再默认 SWAPPED 路线会变差。
last_reviewed: 2026-08-05
status: resolved
garden_status: keep-evidence
garden_reviewed: 2026-08-05
---

# RGB565_SWAPPED 全链路路线（路线 B）真机对照实验

## 背景

`docs/context/knowledge/project/lvgl-display-tuning-log.md` 记录过：LVGL 9.3 升级后显示异常排查期间，"把显示格式改成 `RGB565_SWAPPED` 并移除 flush swap" 后现象更糟，因此仓库长期维持 `RGB565 + flush 手动 swap`。

但那次实验是**双变量叠加**：换格式 + 删 swap 同时做，且背景是 LVGL 9.3 的圆角/绿条纹异常排查，无法单独归咎于 `RGB565_SWAPPED` 配置本身。2026-08-05 在 LVGL 9.5、显示正常的基线上重做干净对照。

## 当前链路（切换前，基线 A）

- `lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565)`
- flush 阶段在 bounce buffer 上手动 `lv_draw_sw_rgb565_swap()` 后发送

## 实验设计（单变量：只改颜色格式 + 是否手动 swap）

| 组 | LVGL 格式 | flush 手动 swap | 预期 |
|---|---|---|---|
| A（基线） | `RGB565` | 开 | 正常 |
| B（路线 B） | `RGB565_SWAPPED` | 关 | 理论上正常 |
| C（对照） | `RGB565_SWAPPED` | 开（双重交换） | 花屏（用于证明配置生效） |

改动点：
- `components/lvgl_port/lv_port_display.c`：`lv_display_set_color_format()` 两处初始化路径（small / single）
- `components/lvgl_port/lv_port_config.h`：`LV_PORT_BYTE_SWAP_ENABLE 1/0`
- 依赖 `CONFIG_LV_DRAW_SW_SUPPORT_RGB565_SWAPPED=y`（`sdkconfig` 中已存在）

## 真机证据（2026-08-05，用户肉眼观察）

- A 组：正常（实验前基线）
- B 组：**显示一切正常**（颜色、圆角、透明混合、条纹均正常）
- C 组：**花屏/颜色错乱** → 证明 B 与 A 之间的差异是配置真实生效，而非改动未生效

三组均编译通过、`app-flash` 成功。

## 结论

1. 在 LVGL 9.5 + 当前显示链路下，`RGB565_SWAPPED` 全链路路线可用且显示正常。
2. 历史"SWAPPED 更差"是 LVGL 9.3 期间双变量叠加的观察，不能单独归咎于 `RGB565_SWAPPED` 配置。
3. 已按用户决定正式切换为路线 B：
   - `lv_port_display.c`：`LV_COLOR_FORMAT_RGB565_SWAPPED`
   - `lv_port_config.h`：`LV_PORT_BYTE_SWAP_ENABLE 0`（flush 不再 swap）
4. `docs/context/knowledge/project/display-render-touch-transfer-pipeline.md` 已同步更新为路线 B 现状。

## 后续注意事项

- 本次验证是主观现象确认（"一切正常"），未做量化对比（CPU 占用、刷新率、功耗）。若后续优化显示性能，可补量化测量。
- 全链路 swapped 会影响 LVGL 内部颜色混合路径（RGB565A8 透明图、圆角混合）；静态主界面正常不代表所有场景都验证过，若出现与颜色混合相关的异常，优先复查本路线而非回退。
- 若需要回到基线 A：改回 `LV_COLOR_FORMAT_RGB565` + `LV_PORT_BYTE_SWAP_ENABLE 1` 即可。
