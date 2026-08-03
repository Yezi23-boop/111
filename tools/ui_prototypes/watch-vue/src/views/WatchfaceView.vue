<script setup>
// 表盘页(一比一对应 setup_scr_screen_main.c Antigravity 重设计):
//   背景 #f6f5f0;日期 42,24;电池 314,24 + bar 280,27 28×14;
//   数字时钟 0,75 410×70 58px 透明底;天气卡 30,246 350×196 圆角36
// 交互(与板端一致):左滑 -> 功能页;顶部下滑 -> 呼出下拉
import weatherDuoyun from '../assets/img/weather_duoyun_RGB565A8_96x96.png'
import { useSwipe } from '../composables/useSwipe'

defineProps({
  time: { type: String, default: '10:09' },
  date: { type: String, default: '周四 06/18' },
  battery: { type: Number, default: 80 },
  weather: { type: Object, default: () => ({ temp: '24', text: '多云', range: '最高 28°C / 最低 19°C' }) },
  wallpaper: { type: String, default: null },
})
const emit = defineEmits(['show-function', 'grab', 'tap'])

// onSwipe:左滑->功能页,下滑->下拉;onTap:点击(用于下拉打开时点表盘收回)
const { down, up } = useSwipe(
  ({ dx, dy }) => {
    if (Math.abs(dx) > Math.abs(dy) && dx < 0) {
      emit('show-function') // 左滑 -> 功能页
    } else if (dy > 0) {
      emit('grab') // 下滑 -> 下拉
    }
  },
  () => emit('tap'),
)
</script>

<template>
  <div
    class="watchface panel"
    :style="wallpaper ? { backgroundImage: 'url(' + wallpaper + ')' } : undefined"
    @pointerdown="down"
    @pointerup="up"
  >
    <!-- 真机顶部 70px 透明手势区(拖动呼出下拉) -->
    <div class="grab-area">
      <div class="grab-handle"></div>
    </div>

    <div class="date">{{ date }}</div>
    <div class="battery-label">{{ battery }}%</div>
    <div class="battery-bar"><span class="battery-fill" :style="{ width: battery >= 100 ? '24px' : '20px' }"></span></div>

    <div class="digital-clock">{{ time }}</div>

    <div class="weather-card">
      <div class="weather-icon"><img :src="weatherDuoyun" alt="天气" /></div>
      <div class="temp">{{ weather.temp }}<span class="temp-unit">°C</span></div>
      <div class="weather-text">{{ weather.text }}</div>
      <div class="weather-range">{{ weather.range }}</div>
    </div>
  </div>
</template>
