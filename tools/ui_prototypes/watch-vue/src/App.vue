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
import GameView from './views/GameView.vue'
import WallpaperView from './views/WallpaperView.vue'
import NotificationView from './views/NotificationView.vue'
import NotificationBubbleView from './views/NotificationBubbleView.vue'
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

// `?fx=1` 仅用于演示全屏模糊与多层列表阴影，不改变正常音乐原型。
const query = new URLSearchParams(window.location.search)
// 聊天中复制链接时，末尾的中英文标点可能被带进 query 参数。
const routeScreen = (query.get('screen') || '').replace(/[，,。；;！？!?]+$/, '')
const visualFxDemo = query.get('fx') === '1'
const freezeAnimations = query.get('freeze') === '1'
const visualFxCatalogDemo = visualFxDemo && query.get('screen') === 'catalog'
const visualPolishDemo = query.get('polish') === '1'
const visualPolishCatalogDemo = visualPolishDemo && query.get('screen') === 'catalog'
const previewScreens = new Set([
  'hermes', 'ai', 'danger', 'games', 'calendar', 'wallpaper', 'ota', 'wifi',
  'music', 'music-picker', 'music-catalog', 'music-account', 'notifications',
  'hermes-inbox', 'hermes-detail',
  'game-2048', 'game-flappy', 'game-dino',
  'notification-bubble',
])
const musicPickerPreview = routeScreen === 'music-picker' || query.get('picker') === '1'
// 显式 screen 是交互/截图入口，优先于 polish；polish 只决定音乐页的视觉方案。
const screen = ref(
  visualFxCatalogDemo || visualPolishCatalogDemo
    ? 'music-catalog'
    : routeScreen === 'function' || routeScreen === 'dropdown'
      ? 'home'
      : routeScreen === 'music-picker'
        ? 'music'
        : previewScreens.has(routeScreen)
          ? routeScreen
          : visualFxDemo || visualPolishDemo
            ? 'music'
            : 'home',
) // home | hermes | ai | danger | games | calendar | wallpaper | ota | wifi | music...
const showFunction = ref(routeScreen === 'function') // 主屏 tileview:表盘页 <-> 功能页
const dropdownOpen = ref(routeScreen === 'dropdown')
// Host 基准固定为 LVGL 模拟器的已播放首曲状态，避免 Vue 首帧落到“未选择歌曲”。
const musicPlaying = ref(true)
const musicStarted = ref(true)
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
  ['起风了', '买辣椒也用券'],
  ['海阔天空', 'Beyond'],
  ['夜空中最亮的星', '逃跑计划'],
  ['如愿', '王菲'],
  ['花海', '周杰伦'],
  ['孤勇者', '陈奕迅'],
  ['贝加尔湖畔', '李健'],
  ['理想', '赵雷'],
  ['安河桥', '宋冬野'],
  ['消愁', '毛不易'],
  ['漠河舞厅', '柳爽'],
  ['水星记', '郭顶'],
  ['岁月神偷', '金玟岐'],
  ['大鱼', '周深'],
  ['向云端', '小霞'],
  ['阿刁', '张韶涵'],
  ['鸿雁', '呼斯楞'],
  ['红色高跟鞋', '蔡健雅'],
  ['晚婚', '江蕙'],
  ['如果可以', '韦礼安'],
  ['一路生花', '温奕心'],
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
    <HermesView
      v-else-if="screen === 'hermes' || screen === 'hermes-inbox' || screen === 'hermes-detail'"
      :initial-view="screen === 'hermes-inbox' ? 'inbox' : screen === 'hermes-detail' ? 'detail' : 'voice'"
      @back="backHome"
    />

    <!-- ===== 小智 AI 聊天页 ===== -->
    <AiChatView v-else-if="screen === 'ai'" @back="backHome" />

    <!-- ===== 危险提醒页 ===== -->
    <DangerView v-else-if="screen === 'danger'" @back="backHome" />

    <!-- ===== OTA 维护页 ===== -->
    <OtaView v-else-if="screen === 'ota'" @back="backHome" />

    <!-- ===== 日历页 ===== -->
    <CalendarView v-else-if="screen === 'calendar'" @back="backHome" />

    <!-- ===== 小游戏菜单 ===== -->
    <GamesView
      v-else-if="screen === 'games'"
      @back="backHome"
      @open="(game) => { screen = 'game-' + game }"
    />

    <!-- ===== 小游戏运行页：对应 mini_games_controller 的三个子画布 ===== -->
    <GameView
      v-else-if="screen === 'game-2048' || screen === 'game-flappy' || screen === 'game-dino'"
      :game="screen.slice(5)"
      @back="screen = 'games'"
    />

    <!-- ===== 壁纸页 ===== -->
    <WallpaperView
      v-else-if="screen === 'wallpaper'"
      @apply="(src) => { wallpaper = src; backHome() }"
    />

    <!-- ===== Wi-Fi 管理页(演示) ===== -->
    <WifiView v-else-if="screen === 'wifi'" @back="backHome" />

    <!-- ===== 通知中心 ===== -->
    <NotificationView
      v-else-if="screen === 'notifications'"
      @back="backHome"
      @open-detail="screen = 'hermes-detail'"
    />

    <!-- ===== 全局 layer_top 通知气泡：对应 watch_notification_center.c ===== -->
    <NotificationBubbleView
      v-else-if="screen === 'notification-bubble'"
      @open="screen = 'hermes'"
    />

    <!-- ===== 功能全屏页:音乐(已 Vue 化) ===== -->
    <template v-else-if="screen === 'music'">
      <SourcesView
        :visual-fx-demo="visualFxDemo"
        :visual-polish-demo="visualPolishDemo || screen === 'music'"
        :freeze-animations="freezeAnimations"
        :initial-picker-open="musicPickerPreview"
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
        :visual-fx-demo="visualFxDemo"
        :visual-polish-demo="visualPolishDemo"
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
</template>
