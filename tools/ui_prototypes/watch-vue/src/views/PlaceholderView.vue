<script setup>
// 占位页:尚未 Vue 化的功能页先走这里,路由骨架就位
defineProps({
  icon: { type: String, default: '⏳' },
  name: { type: String, default: '待开发' },
})
const emit = defineEmits(['back'])
</script>

<template>
  <div class="placeholder">
    <button class="btn-back-pl" @click="emit('back')">‹ 返回</button>
    <div class="ico">{{ icon }}</div>
    <div class="name">{{ name }}</div>
    <div class="hint">对应 LVGL 页面尚未 Vue 化</div>
  </div>
</template>
