<script setup>
// OTA 维护页(一比一对应 ota_maintenance_view.c):
//   深色底 #10131A;返回 40,24 72×56;标题 40,42;状态 40,108;进度条 40,220 330×18;
//   主按钮 40,320 / 取消 210,320 160×54
import { ref } from 'vue'

const emit = defineEmits(['back'])
const state = ref('idle') // idle | downloading | done
const progress = ref(0)
const statusText = ref('等待操作')
const primaryText = ref('检查更新')

function checkUpdate() {
  state.value = 'downloading'
  primaryText.value = '下载更新'
  statusText.value = '检查中...'
  progress.value = 0
  const timer = setInterval(() => {
    progress.value += 8
    if (progress.value >= 100) {
      progress.value = 100
      clearInterval(timer)
      state.value = 'done'
      statusText.value = '没有可用更新'
      primaryText.value = '重新检查'
    }
  }, 300)
}
</script>

<template>
  <div class="ota panel">
    <button class="ota-back" @click="emit('back')">&lt;</button>
    <div class="ota-title">系统维护</div>
    <div class="ota-status">{{ statusText }}</div>

    <div class="ota-bar"><div class="ota-fill" :style="{ width: progress + '%' }"></div></div>
    <div class="ota-progress">{{ progress }}%</div>

    <button class="ota-primary" @click="checkUpdate">{{ primaryText }}</button>
    <button class="ota-cancel" @click="emit('back')">取消</button>
  </div>
</template>
