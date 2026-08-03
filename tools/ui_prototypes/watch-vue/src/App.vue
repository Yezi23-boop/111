<script setup>
// 根组件:整体手表 UI 路由。
// 结构对应模拟器 main.c 的导航模型:
//   主屏 tileview(表盘页 cont_1 <-> 功能页 Function)+ 下拉菜单
//   功能入口 -> 全屏页(音乐已 Vue 化,其余走占位)
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
import NotificationView from './views/NotificationView.vue'
import WifiView from './views/WifiView.vue'
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
]
const FUNC_META = {
  heart: { icon: '❤️', name: '心率' },
  ai: { icon: '🤖', name: '小智 AI' },
  game: { icon: '🎮', name: '游戏' },
  alarm: { icon: '⏰', name: '时钟' },
  mic: { icon: '🎤', name: '麦克风' },
  set: { icon: '⚙️', name: '设置' },
  me: { icon: '👤', name: '用户' },
}
const PAGE_VIEWS = {
  // 已 Vue 化的功能页:key -> 对应视图
  alarm: 'calendar',
  mic: 'ota',
}

const screen = ref('home') // home | page:<key> | hermes | music | music-catalog | music-account
const showFunction = ref(false) // 主屏 tileview:表盘页 <-> 功能页
const dropdownOpen = ref(false)
const musicPlaying = ref(true)
const musicModeIdx = ref(0)
const musicSource = ref('今日推荐')
const MODES = ['列表循环', '单曲循环', '随机播放']
const MODE_ICONS = { 列表循环: '🔁', 单曲循环: '🔂', 随机播放: '🔀' }
const musicModeText = () => MODE_ICONS[MODES[musicModeIdx.value]] + ' ' + MODES[musicModeIdx.value]

function openFunc(key) {
  // 与 events_init_screen_main_function 的入口映射一致
  switch (key) {
    case 'ai':
      screen.value = 'ai' // option_2 -> ai_ui_open
      break
    case 'heart':
      screen.value = 'danger' // option_1 -> danger 危险提醒
      break
    case 'alarm':
      screen.value = 'calendar' // option_5 -> 日历页
      break
    case 'mic':
      screen.value = 'ota' // option_6 -> OTA 维护
      break
    default:
      screen.value = 'page:' + key
  }
}
function backHome() {
  screen.value = 'home'
  dropdownOpen.value = false
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
        @show-function="showFunction = true"
        @grab="dropdownOpen = !dropdownOpen"
        @tap="dropdownOpen && (dropdownOpen = false)"
      />
      <FunctionView :items="FUNC_ITEMS" @open="openFunc" @show-watch="showFunction = false" />
      <!-- Hermes/WiFi/通知 演示入口(真机由对应 controller 手势打开) -->
      <div class="demo-nav">
        <button @click="screen = 'hermes'">Hermes</button>
        <button @click="screen = 'wifi'">WiFi</button>
        <button @click="screen = 'notify'">通知</button>
      </div>
    </div>
    <DropdownView
      v-if="screen === 'home'"
      v-model="dropdownOpen"
      @close="dropdownOpen = false"
      @open-music="screen = 'music'"
    />

    <!-- ===== Hermes 记忆手表页(演示入口) ===== -->
    <HermesView v-else-if="screen === 'hermes'" @back="screen = 'home'" />

    <!-- ===== 小智 AI 聊天页 ===== -->
    <AiChatView v-else-if="screen === 'ai'" @back="screen = 'home'" />

    <!-- ===== 危险提醒页 ===== -->
    <DangerView v-else-if="screen === 'danger'" @back="screen = 'home'" />

    <!-- ===== OTA 维护页 ===== -->
    <OtaView v-else-if="screen === 'ota'" @back="screen = 'home'" />

    <!-- ===== 日历页 ===== -->
    <CalendarView v-else-if="screen === 'calendar'" @back="screen = 'home'" />

    <!-- ===== 通知中心(演示) ===== -->
    <NotificationView v-else-if="screen === 'notify'" @back="screen = 'home'" />

    <!-- ===== Wi-Fi 管理页(演示) ===== -->
    <WifiView v-else-if="screen === 'wifi'" @back="screen = 'home'" />

    <!-- ===== 功能全屏页:音乐(已 Vue 化) ===== -->
    <template v-else-if="screen === 'music'">
      <SourcesView
        :playing="musicPlaying"
        :mode-text="musicModeText()"
        @back="backHome"
        @account="screen = 'music-account'"
        @toggle="musicPlaying = !musicPlaying"
        @open-source="(s) => { musicSource = s; screen = 'music-catalog' }"
        @cycle-mode="musicModeIdx = (musicModeIdx + 1) % MODES.length"
      />
    </template>
    <template v-else-if="screen === 'music-catalog'">
      <CatalogView
        :source="musicSource"
        :playing="musicPlaying"
        @back="screen = 'music'"
        @account="screen = 'music-account'"
        @toggle="musicPlaying = !musicPlaying"
        @open-track="() => {}"
      />
    </template>
    <template v-else-if="screen === 'music-account'">
      <AccountView @back="screen = 'music'" />
    </template>

    <!-- ===== 其余功能页:占位 ===== -->
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
    · 下拉控制:表盘顶部下滑手柄(亮度/音量/音乐开关)<br />
    · 已 Vue 化:表盘 / 下拉 / 功能页 / 音乐;<br />
    &nbsp;&nbsp;其余页(心率/AI/时钟/通知/天气/设置/用户)为占位路由<br />
    · 起服务:<code>npm install</code> 后 <code>npm run dev</code> → <code>http://localhost:8767</code>
  </div>
</template>
