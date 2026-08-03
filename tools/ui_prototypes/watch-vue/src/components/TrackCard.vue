<script setup>
import { computed } from 'vue'

// 播放器主舞台:专辑视觉 + 当前曲目 + 播放状态
const props = defineProps({
  trackName: { type: String, default: '未选择歌曲' },
  artist: { type: String, default: '' },
  state: { type: String, default: 'idle' }, // play | pause | idle
})

const stateLabel = computed(() => {
  if (props.trackName === '未选择歌曲') return '未播放'
  return props.state === 'play' ? '正在播放' : '已暂停'
})
</script>

<template>
  <div class="track-panel" :class="{ playing: state === 'play' }">
    <div class="artwork">
      <div class="cover-lines" aria-hidden="true"></div>
      <div class="cover-core" aria-hidden="true"></div>
    </div>
    <div class="track-copy">
      <div class="track-name">{{ trackName }}</div>
      <div class="track-artist">{{ artist || '选择一首音乐开始' }}</div>
      <div class="state-pill" :class="state">
        <span class="dot"></span>
        <span>{{ stateLabel }}</span>
      </div>
    </div>
  </div>
</template>
