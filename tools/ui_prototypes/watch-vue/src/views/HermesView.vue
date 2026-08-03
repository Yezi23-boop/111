<script setup>
// Hermes 记忆手表页(一比一对应 memory_watch_view.c):
//   背景 #fbfbfa;header 96px:返回 40,24 52×44 / 标题 108,22 / 状态徽章 108,56 / 收件箱 258,26 / 连接点 244,41
//   三个子视图:voice 主页面 / inbox 收件箱 / detail 详情
import { ref, computed } from 'vue'

const view = ref('voice') // voice | inbox | detail
const selected = ref({ time: '', text: '' })
const voicePressing = ref(false)
const voiceInside = ref(true)

const emit = defineEmits(['back'])

const stateText = computed(() => voicePressing.value ? '记录中' : 'Hermes 在线')

// 示例数据(演示渲染结构,真实数据来自 memory_watch service)
const conversation = ref([
  { role: 'user', text: '帮我记住下午三点去取快递' },
  { role: 'other', text: '已记录: 下午三点取快递, 需要时我会提醒你' },
])
const inbox = ref([
  { read: false, time: '今天 14:20', text: 'Hermes: 下午三点的会议, 记得提前准备议题' },
  { read: true, time: '昨天 09:05', text: 'Hermes: 上次你说要买的东西已加入购物清单' },
])

function openDetail(item) {
  item.read = true
  selected.value = { ...item }
  view.value = 'detail'
}

let gestureStartX = 0
let gestureStartY = 0
let gestureTracking = false
function pageDown(e) {
  if (e.target.closest?.('.hermes-voice-btn')) {
    gestureTracking = false
    return
  }
  gestureStartX = e.clientX
  gestureStartY = e.clientY
  gestureTracking = true
}
function pageUp(e) {
  if (!gestureTracking) return
  gestureTracking = false
  const dx = e.clientX - gestureStartX
  const dy = e.clientY - gestureStartY
  if (Math.abs(dy) > 90 || Math.abs(dx) < 70) return
  if (view.value === 'voice' && dx < 0) view.value = 'inbox'
  else if (view.value === 'inbox' && dx > 0) view.value = 'voice'
  else if (view.value === 'detail' && dx > 0) view.value = 'inbox'
}
function pageCancel() {
  gestureTracking = false
}

function pointInside(el, e) {
  if (!el || !e) return true
  const rect = el.getBoundingClientRect()
  return e.clientX >= rect.left && e.clientX <= rect.right &&
    e.clientY >= rect.top && e.clientY <= rect.bottom
}
function voiceDown(e) {
  voicePressing.value = true
  voiceInside.value = true
  e.currentTarget.setPointerCapture?.(e.pointerId)
}
function voiceMove(e) {
  if (voicePressing.value) voiceInside.value = pointInside(e.currentTarget, e)
}
function voiceUp(e) {
  if (!voicePressing.value) return
  voiceMove(e)
  const shouldSend = voiceInside.value
  voicePressing.value = false
  if (shouldSend) {
    conversation.value.push({ role: 'user', text: '已记录一条语音消息' })
  }
}
function voiceCancel() {
  if (!voicePressing.value) return
  voicePressing.value = false
  voiceInside.value = false
}

function goBack() {
  if (view.value === 'voice') emit('back')
  else if (view.value === 'inbox') view.value = 'voice'
  else view.value = 'inbox'
}
</script>

<template>
  <div class="hermes panel">
    <!-- ===== Header(96px) ===== -->
    <button class="hermes-back" @click="goBack">
      ‹
    </button>
    <div class="hermes-title">
      {{ view === 'voice' ? 'Hermes' : view === 'inbox' ? '收件箱' : '消息' }}
    </div>
    <div class="hermes-status-badge">{{ stateText }}</div>
    <button class="hermes-inbox-btn" @click="view = 'inbox'">收件箱</button>
    <div class="hermes-dot"></div>

    <!-- ===== Voice 主页面 ===== -->
    <div
      v-if="view === 'voice'"
      class="hermes-page"
      @pointerdown="pageDown"
      @pointerup="pageUp"
      @pointercancel="pageCancel"
    >
      <div class="hermes-state-label">
        {{ voicePressing ? (voiceInside ? '松开发送' : '松手取消') : '待命' }}
      </div>
      <div class="hermes-conv">
        <template v-for="(m, i) in conversation" :key="i">
          <!-- 系统提示:黄底徽章 -->
          <div v-if="m.role === 'system'" class="hermes-sys">{{ m.text }}</div>
          <!-- 用户消息:右对齐浅灰卡 -->
          <div v-else-if="m.role === 'user'" class="hermes-msg user">{{ m.text }}</div>
          <!-- 对方消息:左对齐白卡 -->
          <div v-else class="hermes-msg other">{{ m.text }}</div>
        </template>
      </div>
      <button
        class="hermes-voice-btn"
        :class="{ pressing: voicePressing, cancel: voicePressing && !voiceInside }"
        @pointerdown="voiceDown"
        @pointermove="voiceMove"
        @pointerup="voiceUp"
        @pointercancel="voiceCancel"
      >
        {{ voicePressing ? (voiceInside ? '松开发送' : '松手取消') : '按住说话' }}
      </button>
    </div>

    <!-- ===== Inbox 收件箱 ===== -->
    <div
      v-else-if="view === 'inbox'"
      class="hermes-page"
      @pointerdown="pageDown"
      @pointerup="pageUp"
      @pointercancel="pageCancel"
    >
      <div class="hermes-subtitle">Hermes 发来的短消息, 只读查看</div>
      <div class="hermes-inbox-list">
        <button
          v-for="(item, i) in inbox"
          :key="i"
          class="hermes-row"
          :class="{ unread: !item.read }"
          @click="openDetail(item)"
        >
          <span v-if="!item.read" class="hermes-unread-dot"></span>
          <span class="hermes-row-time">{{ item.time }}</span>
          <span class="hermes-row-text">{{ item.text }}</span>
        </button>
      </div>
    </div>

    <!-- ===== Detail 详情 ===== -->
    <div
      v-else
      class="hermes-page"
      @pointerdown="pageDown"
      @pointerup="pageUp"
      @pointercancel="pageCancel"
    >
      <div class="hermes-detail-time">{{ selected.time }}</div>
      <div class="hermes-detail-card">{{ selected.text }}</div>
      <div class="hermes-hint">右滑返回收件箱</div>
    </div>
  </div>
</template>
