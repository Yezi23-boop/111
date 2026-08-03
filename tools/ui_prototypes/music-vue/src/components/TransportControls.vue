<script setup>
// 播放控制:上一首 / 播放暂停 / 下一首
// 坐标对齐板端:上一首 40,196 64×64;播放 169,190 72×72;下一首 306,196 64×64
defineProps({
  playing: { type: Boolean, default: true },
})
const emit = defineEmits(['prev', 'toggle', 'next'])
</script>

<template>
  <button class="btn-round btn-prev" @click="emit('prev')">⏮</button>
  <button
    class="btn-round btn-toggle"
    :class="{ paused: !playing }"
    @click="emit('toggle')"
    aria-label="播放/暂停"
  >
    <svg class="play-ico" viewBox="0 0 24 24"><path d="M8 5v14l11-7z" /></svg>
    <svg class="pause-ico" viewBox="0 0 24 24"><path d="M6 5h4v14H6zM14 5h4v14h-4z" /></svg>
  </button>
  <button class="btn-round btn-next" @click="emit('next')">⏭</button>
</template>
