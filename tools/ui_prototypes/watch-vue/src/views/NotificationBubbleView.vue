<script setup>
import WatchfaceView from './WatchfaceView.vue'
import { ref } from 'vue'

const emit = defineEmits(['open'])
const visible = ref(true)
const offsetX = ref(0)
let gesture = null

function beginPointer(event) {
  if (event.pointerType === 'mouse' && event.button !== 0) return
  gesture = { pointerId: event.pointerId, startX: event.clientX, startY: event.clientY, moved: false }
  event.currentTarget.setPointerCapture?.(event.pointerId)
}

function movePointer(event) {
  if (!gesture || gesture.pointerId !== event.pointerId) return
  const dx = event.clientX - gesture.startX
  const dy = event.clientY - gesture.startY
  if (Math.abs(dx) >= 8 || Math.abs(dy) >= 8) gesture.moved = true
  if (dx > 0 && Math.abs(dx) > Math.abs(dy)) {
    offsetX.value = dx
    event.preventDefault()
  }
}

function finishPointer(event) {
  if (!gesture || gesture.pointerId !== event.pointerId) return
  const dx = event.clientX - gesture.startX
  const dy = event.clientY - gesture.startY
  const dismissed = dx >= 48 && dx > Math.abs(dy) * 2
  const pointerId = gesture.pointerId
  gesture = null

  if (dismissed) {
    visible.value = false
    offsetX.value = 0
  } else {
    offsetX.value = 0
  }
  if (event.currentTarget.hasPointerCapture?.(pointerId)) {
    event.currentTarget.releasePointerCapture(pointerId)
  }
}

function openBubble() {
  if (!gesture && visible.value) emit('open')
}
</script>

<template>
  <div class="notification-bubble-demo panel">
    <WatchfaceView />
    <button
      v-if="visible"
      class="notification-bubble"
      :style="{ transform: `translateX(${offsetX}px)` }"
      @click="openBubble"
      @pointerdown="beginPointer"
      @pointermove="movePointer"
      @pointerup="finishPointer"
      @pointercancel="finishPointer"
    >
      <span class="notification-bubble-bar" aria-hidden="true"></span>
      <strong>Hermes 回复已到达</strong>
      <span>点击查看对话</span>
    </button>
  </div>
</template>
