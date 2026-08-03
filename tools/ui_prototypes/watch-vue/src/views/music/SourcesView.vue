<script setup>
// 来源主页:播放卡 + 控制键 + 音乐来源 + 播放模式
// 来源坐标对齐板端:2×2 网格,40,326 起,宽 160 高 50,列距 170 行距 62
import TopBar from '../../components/TopBar.vue'
import TrackCard from '../../components/TrackCard.vue'
import TransportControls from '../../components/TransportControls.vue'

const props = defineProps({
  playing: { type: Boolean, default: true },
  modeText: { type: String, default: '列表循环' },
})
const emit = defineEmits(['back', 'account', 'toggle', 'open-source', 'cycle-mode'])

const SOURCES = [
  { label: '今日推荐', icon: '✦' },
  { label: '我喜欢', icon: '♥' },
  { label: '我的歌单', icon: '☰' },
  { label: '最近播放', icon: '↻' },
]
</script>

<template>
  <TopBar title="音乐" @back="emit('back')" @account="emit('account')" />

  <TrackCard track-name="星辰大海" artist="黄霄雲" :state="playing ? 'play' : 'pause'" />

  <TransportControls :playing="playing" @toggle="emit('toggle')" />

  <div class="section-label">音乐来源</div>
  <button class="btn-mode" @click="emit('cycle-mode')">{{ modeText }}</button>

  <div class="source-grid">
    <button
      v-for="s in SOURCES"
      :key="s.label"
      class="source-btn"
      @click="emit('open-source', s.label)"
    >
      <span class="ico">{{ s.icon }}</span>{{ s.label }}
    </button>
  </div>
</template>
