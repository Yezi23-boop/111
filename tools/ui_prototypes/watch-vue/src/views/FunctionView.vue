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
const CLICK_TOLERANCE_PX = 12
let arcFrame = 0
let listDrag = null

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

function handleListScroll() {
  const list = listRef.value
  if (list && list.scrollLeft !== 0) list.scrollLeft = 0
  scheduleArc()
}

function snapToNearestCard() {
  const list = listRef.value
  const cards = cardRefs.value.filter(Boolean)
  if (!list || cards.length === 0) return

  const viewportCenter = list.scrollTop + list.clientHeight / 2
  const nearest = cards.reduce((current, card) => {
    const currentDistance = Math.abs(
      current.offsetTop + current.offsetHeight / 2 - viewportCenter,
    )
    const nextDistance = Math.abs(
      card.offsetTop + card.offsetHeight / 2 - viewportCenter,
    )
    return nextDistance < currentDistance ? card : current
  })
  const target = nearest.offsetTop + nearest.offsetHeight / 2 - list.clientHeight / 2
  const maxScroll = Math.max(0, list.scrollHeight - list.clientHeight)
  list.scrollTo({
    top: Math.max(0, Math.min(maxScroll, target)),
    behavior: 'smooth',
  })
}

function beginListDrag(e) {
  const list = listRef.value
  if (!list || (e.pointerType === 'mouse' && e.button !== 0)) return

  listDrag = {
    pointerId: e.pointerId,
    startX: e.clientX,
    startY: e.clientY,
    lastY: e.clientY,
    axis: null,
    cardKey: e.target.closest?.('.fn-card')?.dataset.key || null,
  }
  list.classList.add('is-dragging')
  list.setPointerCapture?.(e.pointerId)
}

function moveListDrag(e) {
  if (!listDrag || e.pointerId !== listDrag.pointerId) return

  const dx = e.clientX - listDrag.startX
  const dy = e.clientY - listDrag.startY
  if (!listDrag.axis && Math.max(Math.abs(dx), Math.abs(dy)) > CLICK_TOLERANCE_PX) {
    listDrag.axis = Math.abs(dy) >= Math.abs(dx) ? 'vertical' : 'horizontal'
  }

  if (listDrag.axis === 'vertical') {
    const list = listRef.value
    if (list) {
      list.scrollTop -= e.clientY - listDrag.lastY
      e.preventDefault()
    }
  }
  listDrag.lastY = e.clientY
}

function finishListDrag(e, allowClick = true) {
  if (!listDrag || e.pointerId !== listDrag.pointerId) return

  const pointerId = listDrag.pointerId
  const wasDrag = listDrag.axis !== null
  const cardKey = allowClick && !wasDrag ? listDrag.cardKey : null
  const list = listRef.value
  listDrag = null
  list?.classList.remove('is-dragging')
  if (list?.hasPointerCapture?.(pointerId)) list.releasePointerCapture(pointerId)

  if (wasDrag) {
    snapToNearestCard()
  }

  if (cardKey) openFunction(cardKey)
}

function openFunction(key) {
  emit('open', key)
}

function handleCardClick(e, key) {
  // Pointer 激活由 12px 位移判定处理；detail=0 保留键盘/脚本激活能力。
  if (e.detail === 0) openFunction(key)
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
    <div
      ref="listRef"
      class="function-list"
      @scroll="handleListScroll"
      @pointerdown="beginListDrag"
      @pointermove="moveListDrag"
      @pointerup="finishListDrag"
      @pointercancel="finishListDrag($event, false)"
      @lostpointercapture="finishListDrag($event, false)"
    >
      <button
        v-for="(it, index) in props.items"
        :key="it.key"
        :ref="(el) => setCardRef(el, index)"
        class="fn-card"
        :data-key="it.key"
        :aria-label="it.label"
        :title="it.label"
        @click="handleCardClick($event, it.key)"
      >
        <img v-if="ICONS[it.icon]" :src="ICONS[it.icon]" :alt="it.label" />
        <span v-else class="fn-icon-text" aria-hidden="true">⇩</span>
      </button>
    </div>
  </div>
</template>
