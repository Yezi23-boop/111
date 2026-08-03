<script setup>
// 顶部栏:返回 / 标题 / 登录(坐标对齐板端:返回 40,20 / 标题 112,24 / 登录 304,20)
defineProps({
  title: { type: String, default: '音乐' },
  showAccount: { type: Boolean, default: true },
})
const emit = defineEmits(['back', 'account'])
</script>

<template>
  <div class="top-bar">
    <button class="btn-back" @click="emit('back')"><span class="chev">‹</span>返回</button>
    <div class="title">{{ title }}</div>
    <button v-if="showAccount" class="btn-account" @click="emit('account')">👤 登录</button>
    <button v-else class="btn-account disabled">👤 登录</button>
  </div>
</template>
