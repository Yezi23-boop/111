<script setup>
import wallpaper1 from '../assets/img/yuanjiao1_RGB565A8_180x180.png'
import wallpaper2 from '../assets/img/yuanjiao2_RGB565A8_180x180.png'
import wallpaper3 from '../assets/img/yuanjiao3_RGB565A8_180x180.png'
import wallpaper4 from '../assets/img/yuanjiao4_RGB565A8_180x180.png'
import background1 from '../assets/img/1_RGB565A8_410x502.png'
import background2 from '../assets/img/2_RGB565A8_410x502.png'
import background3 from '../assets/img/3_RGB565A8_410x502.png'
import background4 from '../assets/img/4_RGB565A8_410x502.png'
import { computed, ref } from 'vue'

const emit = defineEmits(['apply'])
const WALLPAPERS = [
  { preview: wallpaper1, background: background1, label: '壁纸 1' },
  { preview: wallpaper2, background: background2, label: '壁纸 2' },
  { preview: wallpaper3, background: background3, label: '壁纸 3' },
  { preview: wallpaper4, background: background4, label: '壁纸 4' },
]

const selected = ref(0)
const selectedName = computed(() => ['小满', '小祁', 'Wechat', 'music'][selected.value])
const drag = ref(null)
let suppressClick = false
let pressTimer

function select(index) {
  selected.value = index
}

function wheelSelect(event) {
  select(event.deltaY > 0 ? Math.min(3, selected.value + 1) : Math.max(0, selected.value - 1))
}

function start(index) {
  clearTimeout(pressTimer)
  pressTimer = setTimeout(() => emit('apply', WALLPAPERS[index].background), 500)
}
function stop() {
  clearTimeout(pressTimer)
}

function beginPointer(event, index = null) {
  if (event.pointerType === 'mouse' && event.button !== 0) return

  drag.value = {
    pointerId: event.pointerId,
    startX: event.clientX,
    startY: event.clientY,
    index: selected.value,
    moved: false,
    axis: null,
  }
  if (index !== null) start(index)
  event.currentTarget.setPointerCapture?.(event.pointerId)
}

function movePointer(event) {
  const state = drag.value
  if (!state || state.pointerId !== event.pointerId) return

  const dx = event.clientX - state.startX
  const dy = event.clientY - state.startY
  if (!state.axis && Math.max(Math.abs(dx), Math.abs(dy)) >= 8) {
    state.axis = Math.abs(dx) >= Math.abs(dy) ? 'horizontal' : 'vertical'
    if (state.axis === 'horizontal') stop()
  }
  if (state.axis !== 'horizontal') return

  state.moved = true
  const next = Math.round(-dx / 190) + state.index
  select(Math.max(0, Math.min(WALLPAPERS.length - 1, next)))
  event.preventDefault()
}

function finishPointer(event) {
  const state = drag.value
  if (!state || state.pointerId !== event.pointerId) return

  stop()
  const pointerId = state.pointerId
  if (state.moved) {
    suppressClick = true
    window.setTimeout(() => { suppressClick = false }, 0)
  }
  drag.value = null
  if (event.currentTarget.hasPointerCapture?.(pointerId)) {
    event.currentTarget.releasePointerCapture(pointerId)
  }
}

function clickSelect(index) {
  if (suppressClick) return
  select(index)
}
</script>

<template>
  <div class="wallpaper panel">
    <div
      class="wallpaper-strip"
      @wheel.prevent="wheelSelect"
      @pointerdown="beginPointer"
      @pointermove="movePointer"
      @pointerup="finishPointer"
      @pointercancel="finishPointer"
    >
      <button
        v-for="(item, index) in WALLPAPERS"
        :key="item.label"
        class="wallpaper-option"
        :data-index="index"
        :aria-label="item.label"
        :class="{ selected: selected === index }"
        :style="{
          left: `${115 + (index - selected) * 190}px`,
          top: `${132 + (Math.abs(index - selected) >= 2 ? 251 : 251 - Math.sqrt(251 * 251 - (Math.abs(index - selected) * 190) ** 2))}px`,
        }"
        @click="clickSelect(index)"
        @pointerdown.stop="beginPointer($event, index)"
      >
        <img :src="item.preview" :alt="item.label" />
      </button>
    </div>
    <div class="wallpaper-label">{{ selectedName }}</div>
  </div>
</template>
