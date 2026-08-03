<script setup>
// 根组件:整体手表 UI 路由。
// 结构对应模拟器 main.c 的导航模型:
//   主屏 tileview(表盘页 cont_1 <-> 功能页 Function)+ 下拉菜单
//   功能入口 -> 全屏页
import { ref } from 'vue'
import WatchScreen from './components/WatchScreen.vue'
import WatchfaceView from './views/WatchfaceView.vue'
import DropdownView from './views/DropdownView.vue'
import FunctionView from './views/FunctionView.vue'
import PlaceholderView from './views/PlaceholderView.vue'
import HermesView from './views/HermesView.vue'
import AiChatView from './views/AiChatView.vue'
import DangerView from './views/DangerView.vue'
import OtaView from './views/OtaView.vue'
import CalendarView from './views/CalendarView.vue'
import WifiView from './views/WifiView.vue'
import GamesView from './views/GamesView.vue'
import WallpaperView from './views/WallpaperView.vue'
import SourcesView from './views/music/SourcesView.vue'
import CatalogView from './views/music/CatalogView.vue'
import AccountView from './views/music/AccountView.vue'

// 功能页入口(与 events_init_screen_main_function 的 option_1/2/4/5/6/7/8 顺序一致)
const FUNC_ITEMS = [
  { key: 'heart', label: '心率', icon: 'heart' },
  { key: 'ai', label: '小智 AI', icon: 'ai' },
  { key: 'game', label: '游戏', icon: 'game' },
  { key: 'alarm', label: '时钟', icon: 'alarm' },
  { key: 'mic', label: '麦克风', icon: 'mic' },
  { key: 'set', label: '设置', icon: 'set' },
  { key: 'me', label: '用户', icon: 'me' },
  { key: 'ota', label: '系统维护', icon: 'ota' },
]
const FUNC_META = {
  heart: { icon: '❤️', name: '心率' },
  ai: { icon: '🤖', name: '小智 AI' },
  game: { icon: '🎮', name: '游戏' },
  alarm: { icon: '⏰', name: '时钟' },
  mic: { icon: '🎤', name: '麦克风' },
  set: { icon: '⚙️', name: '设置' },
  me: { icon: '👤', name: '用户' },
  ota: { icon: '⇩', name: '系统维护' },
}

const screen = ref('home') // home | hermes | ai | danger | games | calendar | wallpaper | ota | wifi | music...
const showFunction = ref(false) // 主屏 tileview:表盘页 <-> 功能页
const dropdownOpen = ref(false)
const musicPlaying = ref(false)
const musicStarted = ref(false)
const musicModeIdx = ref(0)
const musicSource = ref('今日推荐')
const wallpaper = ref(null)
const musicTrackIndex = ref(0)
const MODES = ['列表循环', '单曲循环', '随机播放']
const MUSIC_TRACKS = [
  ['星辰大海', '黄霄雲'],
  ['富士山下', '陈奕迅'],
  ['光年之外', '邓紫棋'],
  ['成都', '赵雷'],
  ['晴天', '周杰伦'],
  ['平凡之路', '朴树'],
  ['七里香', '周杰伦'],
  ['南山南', '马頔'],
]
const musicModeText = () => MODES[musicModeIdx.value]
const musicTrack = () => musicStarted.value ? MUSIC_TRACKS[musicTrackIndex.value] : ['未选择歌曲', '']

function toggleMusic() {
  if (!musicStarted.value) musicStarted.value = true
  musicPlaying.value = !musicPlaying.value
}

function openFunc(key) {
  // 与 events_init_screen_main_function 的入口映射一致
  switch (key) {
    case 'ai':
      screen.value = 'ai' // option_2 -> ai_ui_open
      break
    case 'heart':
      break // option_1 当前没有 LVGL 回调
    case 'game':
      screen.value = 'games' // option_4 -> mini_games_controller_open
      break
    case 'alarm':
      screen.value = 'calendar' // option_5 -> screen_time -> 日历视图
      break
    case 'mic':
      screen.value = 'danger' // option_6 -> danger_detection_ui_open
      break
    case 'set':
      screen.value = 'wallpaper' // option_7 -> screen_wallpaper
      break
    case 'me':
      screen.value = 'hermes' // option_8 -> memory_watch_controller_open
      break
    case 'ota':
      screen.value = 'ota' // 动态维护卡片 -> ota_maintenance_view_open
      break
    default:
      screen.value = 'page:' + key
  }
}
function backHome() {
  screen.value = 'home'
  showFunction.value = false
  dropdownOpen.value = false
}
function openWifi() {
  dropdownOpen.value = false
  screen.value = 'wifi'
}
function selectMusicTrack(track) {
  const index = MUSIC_TRACKS.findIndex(([name, artist]) => name === track[0] && artist === track[1])
  if (index >= 0) musicTrackIndex.value = index
  musicStarted.value = true
  musicPlaying.value = true
}
function stepMusicTrack(step) {
  musicStarted.value = true
  musicTrackIndex.value = (musicTrackIndex.value + step + MUSIC_TRACKS.length) % MUSIC_TRACKS.length
  musicPlaying.value = true
}
</script>

<template>
  <WatchScreen :theme="screen.startsWith('music') ? 'dark' : ''">
    <!-- ===== 主屏:tileview(表盘 <-> 功能页)+ 下拉 ===== -->
    <div
      v-if="screen === 'home'"
      class="screen-main"
      :class="{ 'show-function': showFunction }"
    >
      <WatchfaceView
        :wallpaper="wallpaper"
        @show-function="showFunction = true"
        @grab="dropdownOpen = true"
        @tap="dropdownOpen && (dropdownOpen = false)"
      />
      <FunctionView :items="FUNC_ITEMS" @open="openFunc" @show-watch="showFunction = false" />
    </div>
    <DropdownView
      v-if="screen === 'home'"
      v-model="dropdownOpen"
      @close="dropdownOpen = false"
      :music-playing="musicPlaying"
      @open-wifi="openWifi"
      @toggle-music="toggleMusic"
      @open-music="screen = 'music'; dropdownOpen = false"
    />

    <!-- ===== Hermes 记忆手表页(演示入口) ===== -->
    <HermesView v-else-if="screen === 'hermes'" @back="backHome" />

    <!-- ===== 小智 AI 聊天页 ===== -->
    <AiChatView v-else-if="screen === 'ai'" @back="backHome" />

    <!-- ===== 危险提醒页 ===== -->
    <DangerView v-else-if="screen === 'danger'" @back="backHome" />

    <!-- ===== OTA 维护页 ===== -->
    <OtaView v-else-if="screen === 'ota'" @back="backHome" />

    <!-- ===== 日历页 ===== -->
    <CalendarView v-else-if="screen === 'calendar'" @back="backHome" />

    <!-- ===== 小游戏菜单 ===== -->
    <GamesView v-else-if="screen === 'games'" @back="backHome" />

    <!-- ===== 壁纸页 ===== -->
    <WallpaperView
      v-else-if="screen === 'wallpaper'"
      @apply="(src) => { wallpaper = src; backHome() }"
    />

    <!-- ===== Wi-Fi 管理页(演示) ===== -->
    <WifiView v-else-if="screen === 'wifi'" @back="backHome" />

    <!-- ===== 功能全屏页:音乐(已 Vue 化) ===== -->
    <template v-else-if="screen === 'music'">
      <SourcesView
        :playing="musicPlaying"
        :mode-text="musicModeText()"
        :track-name="musicTrack()[0]"
        :artist="musicTrack()[1]"
        @back="backHome"
        @account="screen = 'music-account'"
        @toggle="toggleMusic"
        @prev="stepMusicTrack(-1)"
        @next="stepMusicTrack(1)"
        @open-source="(s) => { musicSource = s; screen = 'music-catalog' }"
        @cycle-mode="musicModeIdx = (musicModeIdx + 1) % MODES.length"
      />
    </template>
    <template v-else-if="screen === 'music-catalog'">
      <CatalogView
        :source="musicSource"
        :playing="musicPlaying"
        :track-name="musicTrack()[0]"
        :artist="musicTrack()[1]"
        @back="screen = 'music'"
        @account="screen = 'music-account'"
        @toggle="toggleMusic"
        @prev="stepMusicTrack(-1)"
        @next="stepMusicTrack(1)"
        @open-track="selectMusicTrack"
        @exit="backHome"
      />
    </template>
    <template v-else-if="screen === 'music-account'">
      <AccountView @back="screen = 'music'" />
    </template>

    <!-- ===== 未绑定到当前 LVGL 回调的保留入口 ===== -->
    <PlaceholderView
      v-else
      :icon="FUNC_META[screen.slice(5)]?.icon || '⏳'"
      :name="FUNC_META[screen.slice(5)]?.name || screen"
      @back="backHome"
    />
  </WatchScreen>

  <div class="note-box">
    <b>工具/ui_prototypes/watch-vue</b> — 模拟器整体 UI 的 Vue 版<br />
    · 主屏 tileview:表盘页 ↔ 功能页(切换见代码,原型里点击顶部手柄/功能入口)<br />
    · 下拉控制:表盘顶部下滑手柄(亮度/音量/Wi-Fi/蓝牙/音乐)<br />
    · 功能入口对应:AI / 游戏 / 时钟日历 / 危险识别 / 壁纸 / Hermes / OTA;<br />
    &nbsp;&nbsp;心率入口保留为当前 LVGL 的无回调状态<br />
    · 起服务:<code>npm install</code> 后 <code>npm run dev</code> → <code>http://localhost:8767</code>
  </div>
</template>
