<script setup>
// 播放控制:上一首 / 播放暂停 / 下一首
defineProps({
  playing: { type: Boolean, default: true },
})
const emit = defineEmits(['prev', 'toggle', 'next'])
</script>

<template>
  <div class="transport">
    <button class="control-button" aria-label="上一首" @click="emit('prev')">
      <span class="skip-glyph skip-glyph--prev" aria-hidden="true"></span>
    </button>
    <button
      class="control-button control-button--main"
      :aria-label="playing ? '暂停' : '播放'"
      @click="emit('toggle')"
    >
      <span v-if="playing" class="pause-glyph" aria-hidden="true"></span>
      <span v-else class="play-glyph" aria-hidden="true"></span>
    </button>
    <button class="control-button" aria-label="下一首" @click="emit('next')">
      <span class="skip-glyph skip-glyph--next" aria-hidden="true"></span>
    </button>
  </div>
</template>
