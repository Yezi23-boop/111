<script setup>
// 功能页(一比一对应 screen_main_Function_main):
//   黑底 + 紫卡入口 256×90 圆角18 + 70×70 真实图标(图标在卡内 13,4)
// 交互(与板端一致):右滑 -> 表盘页;入口点击(位移<=12px)打开对应页
import heart from '../assets/img/heart_RGB565A8_70x70.png'
import ai from '../assets/img/ai_RGB565A8_70x70.png'
import game from '../assets/img/game_RGB565A8_70x70.png'
import alarm from '../assets/img/alarm_clock_RGB565A8_70x70.png'
import mic from '../assets/img/Microphone_RGB565A8_70x70.png'
import set from '../assets/img/set_RGB565A8_70x70.png'
import me from '../assets/img/me_RGB565A8_70x70.png'
import { nextTick, onBeforeUnmount, onMounted, ref } from 'vue'
import { useSwipe } from '../composables/useSwipe'

const props = defineProps({
  items: { type: Array, required: true },
})
const emit = defineEmits(['open', 'show-watch'])

const ICONS = { heart, ai, game, alarm, mic, set, me }

const listRef = ref(null)
const cardRefs = ref([])
let arcFrame = 0

function setCardRef(el, index) {
  if (el) cardRefs.value[index] = el
}

function updateArc() {
  const list = listRef.value
  if (!list) return

  const centerY = list.clientHeight / 2
  const radius = list.clientWidth * 0.7
  for (const card of cardRefs.value) {
    if (!card) continue
    const cardCenterY = card.offsetTop - list.scrollTop + card.offsetHeight / 2
    const distance = Math.abs(cardCenterY - centerY)
    const x = distance >= radius
      ? radius
      : radius - Math.sqrt(radius * radius - distance * distance)
    card.style.transform = `translateX(${Math.round(x + 30)}px)`
    card.style.opacity = String(Math.max(0, 1 - x / radius))
  }
}

function scheduleArc() {
  cancelAnimationFrame(arcFrame)
  arcFrame = requestAnimationFrame(updateArc)
}

onMounted(async () => {
  await nextTick()
  // LVGL setup_vertical_scroll() 初次只把首项滚入可视区域；末项居中是 host 截图入口额外做的事。
  listRef.value.scrollTop = 0
  updateArc()
})

onBeforeUnmount(() => cancelAnimationFrame(arcFrame))

// 页面级右滑切回表盘
const { down: pageDown, up: pageUp, cancel: pageCancel } = useSwipe(({ dx, dy }) => {
  if (dx >= 70 && dx > Math.abs(dy) * 2) emit('show-watch')
})
// 入口级点击(位移<=12px)与右滑,都用入口自身的 down/up 防抖
</script>

<template>
  <div
    class="function-page panel"
    @pointerdown="pageDown"
    @pointerup="pageUp"
    @pointercancel="pageCancel"
  >
    <div ref="listRef" class="function-list" @scroll="scheduleArc">
      <button
        v-for="(it, index) in props.items"
        :key="it.key"
        :ref="(el) => setCardRef(el, index)"
        class="fn-card"
        @click="emit('open', it.key)"
      >
        <img v-if="ICONS[it.icon]" :src="ICONS[it.icon]" :alt="it.label" />
        <span v-else class="fn-icon-text" aria-hidden="true">⇩</span>
      </button>
    </div>
  </div>
</template>
