<script setup>
// 通知中心(一比一对应 watch_notification_center.c):
//   气泡 40,20 330×88 圆角24 绿底 #C4D2C2 + 粉色竖条 6×44 #D85A7A;
//   右滑 48px 以上消除(NC_SWIPE_DISMISS_PX)
import { ref } from 'vue'
import { useSwipe } from '../composables/useSwipe'

const emit = defineEmits(['back'])
const bubble = ref({ title: 'Hermes 提醒', preview: '下午 3:00 和产品组开会', visible: true })

// 右滑消除气泡
const { down, up } = useSwipe(({ dx }) => {
  if (dx > 48) bubble.value.visible = false
})
</script>

<template>
  <div class="nc panel">
    <button class="nc-back" @click="emit('back')">‹ 返回</button>
    <div class="nc-title">通知</div>

    <!-- 通知气泡(可右滑消除) -->
    <div
      v-if="bubble.visible"
      class="nc-bubble"
      @pointerdown="down"
      @pointerup="up"
    >
      <div class="nc-pink-bar"></div>
      <div class="nc-bubble-title">{{ bubble.title }}</div>
      <div class="nc-bubble-preview">{{ bubble.preview }}</div>
    </div>
    <div v-else class="nc-empty">暂无通知</div>
  </div>
</template>
