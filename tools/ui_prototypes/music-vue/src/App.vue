<script setup>
// 根组件:持有共享状态(当前视图 / 播放 / 模式),按视图切换渲染,示范 Vue 数据流
import { ref } from 'vue'
import WatchScreen from './components/WatchScreen.vue'
import SourcesView from './views/SourcesView.vue'
import CatalogView from './views/CatalogView.vue'
import AccountView from './views/AccountView.vue'

const view = ref('sources') // sources | catalog | account
const playing = ref(true)
const modeIdx = ref(0)
const currentSource = ref('今日推荐')

const MODES = ['列表循环', '单曲循环', '随机播放']
const MODE_ICONS = { 列表循环: '🔁', 单曲循环: '🔂', 随机播放: '🔀' }
const modeText = () => MODE_ICONS[MODES[modeIdx.value]] + ' ' + MODES[modeIdx.value]

function openSource(label) {
  currentSource.value = label
  view.value = 'catalog'
}
</script>

<template>
  <WatchScreen>
    <SourcesView
      v-if="view === 'sources'"
      :playing="playing"
      :mode-text="modeText()"
      @back="view = 'sources'"
      @account="view = 'account'"
      @toggle="playing = !playing"
      @open-source="openSource"
      @cycle-mode="modeIdx = (modeIdx + 1) % MODES.length"
    />
    <CatalogView
      v-else-if="view === 'catalog'"
      :source="currentSource"
      :playing="playing"
      @back="view = 'sources'"
      @account="view = 'account'"
      @toggle="playing = !playing"
      @open-track="() => {}"
    />
    <AccountView v-else @back="view = 'sources'" />
  </WatchScreen>

  <div class="note-box">
    <b>工具/ui_prototypes/music-vue</b>(Vite + Vue 3)<br />
    · 画布 410×502,坐标对齐板端 <code>music_view.c</code><br />
    · 组件划分:外壳 / 顶部栏 / 播放卡 / 控制键 / 三个视图<br />
    · 共享状态(视图/播放/模式)在 <code>App.vue</code>,props 下发、emit 上抛<br />
    · 起服务:<code>npm install</code> 后 <code>npm run dev</code>,打开 <code>http://localhost:8766</code>
  </div>
</template>

<style scoped>
.note-box {
  color: var(--text-dim);
  font-size: 13px;
  line-height: 1.9;
  max-width: 340px;
  align-self: center;
}
.note-box b {
  color: var(--text-mid);
}
.note-box code {
  background: #1a2233;
  padding: 1px 6px;
  border-radius: 5px;
  color: var(--accent-soft);
  font-size: 12px;
}
</style>
