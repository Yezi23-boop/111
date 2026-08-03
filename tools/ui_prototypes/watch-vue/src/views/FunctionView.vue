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
import { useSwipe } from '../composables/useSwipe'

const props = defineProps({
  items: { type: Array, required: true },
})
const emit = defineEmits(['open', 'show-watch'])

const ICONS = { heart, ai, game, alarm, mic, set, me }

// 页面级右滑切回表盘
const { down: pageDown, up: pageUp } = useSwipe(({ dx }) => {
  if (dx > 0) emit('show-watch')
})
// 入口级点击(位移<=12px)与右滑,都用入口自身的 down/up 防抖
</script>

<template>
  <div class="function-page panel" @pointerdown="pageDown" @pointerup="pageUp">
    <div class="function-list">
      <button
        v-for="it in props.items"
        :key="it.key"
        class="fn-card"
        @click="emit('open', it.key)"
      >
        <img :src="ICONS[it.icon]" :alt="it.label" />
      </button>
    </div>
  </div>
</template>
