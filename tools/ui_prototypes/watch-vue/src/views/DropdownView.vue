<script setup>
// 下拉控制菜单(一比一对应 screen_main_Dropdown_menu):
//   Wifi 21,51 76×76 / 蓝牙 121,49 76×76 / 音乐按钮 53,145 134×42
//   亮度滑条 233,8 50×180 / liandu 236,141 46×46
//   音量滑条 331,8 50×180 / shengyin 332,141 46×46
import { ref } from 'vue'
import wifiOff from '../assets/img/WIFI4_RGB565A8_76x76.png'
import wifiOn from '../assets/img/WIFI2_RGB565A8_76x76.png'
import btOff from '../assets/img/langya4_RGB565A8_76x76.png'
import btOn from '../assets/img/langya2_RGB565A8_76x76.png'
import lianduImg from '../assets/img/liandu_RGB565A8_46x46.png'
import shengyinImg from '../assets/img/shengyin_RGB565A8_46x46.png'
import { useSwipe } from '../composables/useSwipe'

const emit = defineEmits(['close', 'open-music'])
const open = defineModel({ type: Boolean, default: false })
const wifiOnFlag = ref(false)
const btOnFlag = ref(false)
const musicOn = ref(false)
const volume = ref(50)
const brightness = ref(50)

// 上滑收回(与板端拖动收回一致)
const { down: ddDown, up: ddUp } = useSwipe(({ dy }) => {
  if (dy < 0) emit('close')
})

// 竖滑条拖动:按 pointer 在滑条内的相对位置计算百分比
function setSlider(target, e, selector) {
  const el = document.querySelector(selector)
  if (!el) return
  const rect = el.getBoundingClientRect()
  const update = (ev) => {
    const ratio = 1 - Math.min(1, Math.max(0, (ev.clientY - rect.top) / rect.height))
    target.value = Math.round(ratio * 100)
  }
  update(e)
  const move = (ev) => update(ev)
  const stop = () => {
    window.removeEventListener('pointermove', move)
    window.removeEventListener('pointerup', stop)
  }
  window.addEventListener('pointermove', move)
  window.addEventListener('pointerup', stop)
}
</script>

<template>
  <div class="dropdown" :class="{ open }" @pointerdown="ddDown" @pointerup="ddUp">
    <button class="dd-icon-btn dd-wifi" @click="wifiOnFlag = !wifiOnFlag" aria-label="WiFi">
      <img :src="wifiOnFlag ? wifiOn : wifiOff" alt="WiFi" />
    </button>
    <button class="dd-icon-btn dd-bt" @click="btOnFlag = !btOnFlag" aria-label="蓝牙">
      <img :src="btOnFlag ? btOn : btOff" alt="蓝牙" />
    </button>

    <button class="dd-music" :class="{ on: musicOn }" @click="emit('open-music')">
      {{ musicOn ? '音乐 开' : '音乐 关' }}
    </button>

    <!-- 亮度竖滑条(拖动改值) -->
    <div
      class="dd-slider bright"
      @pointerdown="(e) => setSlider(brightness, e, '.dd-slider.bright')"
    >
      <div class="track">
        <div class="fill" :style="{ height: brightness + '%' }"></div>
        <div class="knob" :style="{ bottom: 'calc(' + brightness + '% - 9px)' }"></div>
      </div>
    </div>
    <button class="dd-mini img1"><img :src="lianduImg" alt="连读" /></button>

    <!-- 音量竖滑条(拖动改值) -->
    <div
      class="dd-slider loud"
      @pointerdown="(e) => setSlider(volume, e, '.dd-slider.loud')"
    >
      <div class="track">
        <div class="fill" :style="{ height: volume + '%' }"></div>
        <div class="knob" :style="{ bottom: 'calc(' + volume + '% - 9px)' }"></div>
      </div>
    </div>
    <button class="dd-mini imgbtn" @click="musicOn = !musicOn">
      <img :src="shengyinImg" alt="声音" />
    </button>
  </div>
</template>
