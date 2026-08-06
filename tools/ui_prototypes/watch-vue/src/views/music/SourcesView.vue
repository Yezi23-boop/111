<script setup>
// 音乐主页:当前曲目 + 播放控制 + 来源浏览
import { ref } from 'vue'
import TopBar from '../../components/TopBar.vue'
import TrackCard from '../../components/TrackCard.vue'
import TransportControls from '../../components/TransportControls.vue'

const props = defineProps({
  playing: { type: Boolean, default: true },
  modeText: { type: String, default: '列表循环' },
  trackName: { type: String, default: '未选择歌曲' },
  artist: { type: String, default: '' },
})
const emit = defineEmits(['back', 'account', 'toggle', 'prev', 'next', 'open-source', 'cycle-mode'])

const SOURCES = [
  { label: '今日推荐' },
  { label: '我喜欢' },
  { label: '我的歌单' },
  { label: '最近播放' },
]
const sourcePickerOpen = ref(false)

function handleBack() {
  if (sourcePickerOpen.value) {
    sourcePickerOpen.value = false
    return
  }
  emit('back')
}
</script>

<template>
  <div
    class="music-page music-page--sources"
  >
    <TopBar title="音乐" @back="handleBack" @account="emit('account')" />

    <TrackCard
      :track-name="trackName"
      :artist="artist"
      :state="playing ? 'play' : 'idle'"
      role="button"
      tabindex="0"
      aria-label="当前播放曲目，点击进入选择歌单"
      @click="sourcePickerOpen = true"
      @keydown.enter="sourcePickerOpen = true"
      @keydown.space.prevent="sourcePickerOpen = true"
    />

    <TransportControls
      :playing="playing"
      @prev="emit('prev')"
      @toggle="emit('toggle')"
      @next="emit('next')"
    />

    <section class="source-section" aria-label="音乐来源">
      <div class="section-heading">
        <div>
          <div class="section-kicker">EXPLORE</div>
          <h2>音乐来源</h2>
        </div>
        <button class="mode-button" aria-label="切换播放模式" @click="emit('cycle-mode')">
          <span class="mode-mark" aria-hidden="true"></span>
          {{ modeText }}
        </button>
      </div>

      <div class="source-grid">
        <button
          v-for="s in SOURCES"
          :key="s.label"
          class="source-tile"
          @click="emit('open-source', s.label)"
        >
          <span class="source-name">{{ s.label }}</span>
          <span class="source-arrow" aria-hidden="true"></span>
        </button>
      </div>
    </section>

    <section v-if="sourcePickerOpen" class="source-picker" aria-label="选择歌单">
      <div class="source-picker__header">
        <h2>选择歌单</h2>
      </div>
      <div class="source-picker__grid">
        <button
          v-for="s in SOURCES"
          :key="s.label"
          class="source-picker__tile"
          @click="emit('open-source', s.label)"
        >
          <span>{{ s.label }}</span>
          <span class="source-arrow" aria-hidden="true"></span>
        </button>
      </div>
    </section>
  </div>
</template>
