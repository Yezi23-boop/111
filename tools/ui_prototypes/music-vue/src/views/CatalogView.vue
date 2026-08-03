<script setup>
// 歌曲目录页:播放卡 + 控制键 + 来源返回按钮 + 可滚动歌曲列表
// 列表坐标对齐板端:40,326 330×146;行 318×44 行距 8
import TopBar from '../components/TopBar.vue'
import TrackCard from '../components/TrackCard.vue'
import TransportControls from '../components/TransportControls.vue'

const props = defineProps({
  source: { type: String, default: '今日推荐' },
  playing: { type: Boolean, default: true },
})
const emit = defineEmits(['back', 'account', 'toggle', 'open-track'])

// 各来源示例歌曲(纯展示数据)
const LIBRARY = {
  今日推荐: [['星辰大海', '黄霄雲'], ['富士山下', '陈奕迅'], ['光年之外', '邓紫棋'], ['成都', '赵雷']],
  我喜欢: [['晴天', '周杰伦'], ['平凡之路', '朴树'], ['七里香', '周杰伦'], ['南山南', '马頔']],
  我的歌单: [['贝加尔湖畔', '李健'], ['理想', '赵雷'], ['安河桥', '宋冬野'], ['消愁', '毛不易']],
  最近播放: [['孤勇者', '陈奕迅'], ['向云端', '小霞'], ['阿刁', '张韶涵'], ['鸿雁', '呼斯楞']],
}
const NOTES = ['♪', '♫', '♬', '♩']

const songs = LIBRARY[props.source] || []
</script>

<template>
  <TopBar :title="source" @back="emit('back')" @account="emit('account')" />

  <TrackCard track-name="星辰大海" artist="黄霄雲" :state="playing ? 'play' : 'pause'" />

  <TransportControls :playing="playing" @toggle="emit('toggle')" />

  <button class="btn-catalog-back" @click="emit('back')">‹ 来源</button>

  <div class="catalog-list">
    <button
      v-for="(t, i) in songs"
      :key="t[0]"
      class="song-row"
      @click="emit('open-track', t[0])"
    >
      <span class="note">{{ NOTES[i % 4] }}</span>
      <span class="meta">
        <span class="name">{{ t[0] }}</span>
        <span class="sub">{{ t[1] }}</span>
      </span>
      <span class="chev">›</span>
    </button>
  </div>
</template>
