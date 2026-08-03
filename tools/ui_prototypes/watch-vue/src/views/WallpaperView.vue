<script setup>
import wallpaper1 from '../assets/img/yuanjiao1_RGB565A8_180x180.png'
import wallpaper2 from '../assets/img/yuanjiao2_RGB565A8_180x180.png'
import wallpaper3 from '../assets/img/yuanjiao3_RGB565A8_180x180.png'
import wallpaper4 from '../assets/img/yuanjiao4_RGB565A8_180x180.png'
import background1 from '../assets/img/1_RGB565A8_410x502.png'
import background2 from '../assets/img/2_RGB565A8_410x502.png'
import background3 from '../assets/img/3_RGB565A8_410x502.png'
import background4 from '../assets/img/4_RGB565A8_410x502.png'

const emit = defineEmits(['apply'])
const WALLPAPERS = [
  { preview: wallpaper1, background: background1, label: '壁纸 1' },
  { preview: wallpaper2, background: background2, label: '壁纸 2' },
  { preview: wallpaper3, background: background3, label: '壁纸 3' },
  { preview: wallpaper4, background: background4, label: '壁纸 4' },
]

let timer
function start(index) {
  clearTimeout(timer)
  timer = setTimeout(() => emit('apply', WALLPAPERS[index].background), 500)
}
function stop() {
  clearTimeout(timer)
}
</script>

<template>
  <div class="wallpaper panel">
    <div class="wallpaper-title">壁纸</div>
    <div class="wallpaper-strip">
      <button
        v-for="(item, index) in WALLPAPERS"
        :key="item.label"
        class="wallpaper-option"
        :aria-label="item.label"
        @pointerdown="start(index)"
        @pointerup="stop"
        @pointerleave="stop"
        @pointercancel="stop"
      >
        <img :src="item.preview" :alt="item.label" />
      </button>
    </div>
    <div class="wallpaper-hint">长按选择壁纸</div>
  </div>
</template>
