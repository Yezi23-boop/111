<script setup>
// 音乐播放器顶部导航:返回 / 页面标题 / 账号入口
defineProps({
  title: { type: String, default: '音乐' },
  showAccount: { type: Boolean, default: true },
})
const emit = defineEmits(['back', 'account'])
</script>

<template>
  <div class="music-header">
    <button class="music-nav-button" aria-label="返回" @click="emit('back')">
      <span class="back-glyph" aria-hidden="true"></span>
    </button>
    <div class="music-wordmark" aria-label="音乐">
      <span class="music-wordmark-label">MUSIC</span>
      <span class="music-wordmark-title">{{ title }}</span>
    </div>
    <button
      v-if="showAccount"
      class="music-account-button"
      aria-label="打开账号"
      @click="emit('account')"
    >
      登录
    </button>
    <span v-else class="music-account-button" aria-hidden="true">账号</span>
  </div>
</template>
