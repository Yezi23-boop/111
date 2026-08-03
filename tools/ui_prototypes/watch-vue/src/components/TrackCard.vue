<script setup>
// 正在播放卡片:唱片封面(播放时旋转)+ 歌名 + 歌手 + 状态胶囊
// 坐标对齐板端:卡片 40,76 330×100;封面 14,14 72×72;文字自 100 起
defineProps({
  trackName: { type: String, default: '未选择歌曲' },
  artist: { type: String, default: '' },
  state: { type: String, default: 'idle' }, // play | pause | idle
})
</script>

<template>
  <div class="track-panel" :class="{ playing: state === 'play' }">
    <div class="artwork">
      <div class="vinyl"><div class="hub"></div></div>
    </div>
    <div class="track-name">{{ trackName }}</div>
    <div class="track-artist">{{ artist }}</div>
    <div class="state-pill" :class="state">
      <span class="dot"></span>
      <span>{{ state === 'play' ? '正在播放' : state === 'pause' ? '已暂停' : '未播放' }}</span>
    </div>
  </div>
</template>
