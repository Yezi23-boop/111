<script setup>
// 歌曲队列页:当前曲目 + 播放控制 + 来源队列
import { computed } from 'vue'
import TopBar from '../../components/TopBar.vue'
import TrackCard from '../../components/TrackCard.vue'
import TransportControls from '../../components/TransportControls.vue'

const props = defineProps({
  source: { type: String, default: '今日推荐' },
  playing: { type: Boolean, default: true },
  trackName: { type: String, default: '未选择歌曲' },
  artist: { type: String, default: '' },
})
const emit = defineEmits(['back', 'account', 'toggle', 'prev', 'next', 'open-track'])

// 各来源示例歌曲(纯展示数据)
const LIBRARY = {
  今日推荐: [
    ['星辰大海', '黄霄雲'], ['富士山下', '陈奕迅'], ['光年之外', '邓紫棋'], ['成都', '赵雷'],
    ['起风了', '买辣椒也用券'], ['海阔天空', 'Beyond'], ['夜空中最亮的星', '逃跑计划'], ['平凡之路', '朴树'],
  ],
  我喜欢: [
    ['晴天', '周杰伦'], ['平凡之路', '朴树'], ['七里香', '周杰伦'], ['南山南', '马頔'],
    ['如愿', '王菲'], ['花海', '周杰伦'], ['光年之外', '邓紫棋'], ['孤勇者', '陈奕迅'],
  ],
  我的歌单: [
    ['贝加尔湖畔', '李健'], ['理想', '赵雷'], ['安河桥', '宋冬野'], ['消愁', '毛不易'],
    ['漠河舞厅', '柳爽'], ['水星记', '郭顶'], ['岁月神偷', '金玟岐'], ['大鱼', '周深'],
  ],
  最近播放: [
    ['孤勇者', '陈奕迅'], ['向云端', '小霞'], ['阿刁', '张韶涵'], ['鸿雁', '呼斯楞'],
    ['红色高跟鞋', '蔡健雅'], ['晚婚', '江蕙'], ['如果可以', '韦礼安'], ['一路生花', '温奕心'],
  ],
}
const songs = computed(() => LIBRARY[props.source] || [])
</script>

<template>
  <div class="music-page music-page--catalog">
    <TopBar title="音乐" @back="emit('back')" @account="emit('account')" />

    <div class="catalog-heading">
      <div class="section-kicker">COLLECTION</div>
      <h1>{{ source }}</h1>
    </div>

    <TrackCard :track-name="trackName" :artist="artist" :state="playing ? 'play' : 'idle'" />

    <TransportControls
      :playing="playing"
      @prev="emit('prev')"
      @toggle="emit('toggle')"
      @next="emit('next')"
    />

    <div class="catalog-list" aria-label="歌曲列表">
      <button
        v-for="(t, i) in songs"
        :key="t[0]"
        class="song-row"
        :class="{ selected: trackName === t[0] && artist === t[1] }"
        @click="emit('open-track', t)"
      >
        <span class="song-index">{{ String(i + 1).padStart(2, '0') }}</span>
        <span class="song-meta">
          <span class="song-name">{{ t[0] }}</span>
          <span class="song-artist">{{ t[1] }}</span>
        </span>
        <span class="song-arrow" aria-hidden="true"></span>
      </button>
    </div>
  </div>
</template>
