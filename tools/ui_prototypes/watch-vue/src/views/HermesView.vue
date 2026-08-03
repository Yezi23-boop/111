<script setup>
// Hermes 记忆手表页(一比一对应 memory_watch_view.c):
//   背景 #fbfbfa;header 96px:返回 40,24 52×44 / 标题 108,22 / 状态徽章 108,56 / 收件箱 258,26 / 连接点 244,41
//   三个子视图:voice 主页面 / inbox 收件箱 / detail 详情
import { ref } from 'vue'

const view = ref('voice') // voice | inbox | detail
const selected = ref({ time: '', text: '' })

const emit = defineEmits(['back'])

const stateText = ref('未连接')

// 示例数据(演示渲染结构,真实数据来自 memory_watch service)
const CONVERSATION = [
  { role: 'system', text: 'Hermes 已就绪, 按住下方按钮开始语音记录' },
  { role: 'user', text: '今天下午三点和产品组开会' },
  { role: 'other', text: '已记下, 需要我到时候提醒你吗?' },
]
const INBOX = [
  { read: false, time: '今天 14:20', text: 'Hermes: 下午三点的会议, 记得提前准备议题' },
  { read: true, time: '昨天 09:05', text: 'Hermes: 上次你说要买的东西已加入购物清单' },
]

function openDetail(item) {
  selected.value = item
  view.value = 'detail'
}
</script>

<template>
  <div class="hermes panel">
    <!-- ===== Header(96px) ===== -->
    <button class="hermes-back" @click="view === 'voice' ? emit('back') : view === 'inbox' ? (view = 'voice') : (view = 'inbox')">
      ‹
    </button>
    <div class="hermes-title">
      {{ view === 'voice' ? 'Hermes' : view === 'inbox' ? '收件箱' : '消息' }}
    </div>
    <div class="hermes-status-badge">{{ stateText }}</div>
    <button class="hermes-inbox-btn" @click="view = 'inbox'">收件箱</button>
    <div class="hermes-dot"></div>

    <!-- ===== Voice 主页面 ===== -->
    <div v-if="view === 'voice'" class="hermes-page">
      <div class="hermes-state-label">按住说话开始记录</div>
      <div class="hermes-conv">
        <template v-for="(m, i) in CONVERSATION" :key="i">
          <!-- 系统提示:黄底徽章 -->
          <div v-if="m.role === 'system'" class="hermes-sys">{{ m.text }}</div>
          <!-- 用户消息:右对齐浅灰卡 -->
          <div v-else-if="m.role === 'user'" class="hermes-msg user">{{ m.text }}</div>
          <!-- 对方消息:左对齐白卡 -->
          <div v-else class="hermes-msg other">{{ m.text }}</div>
        </template>
      </div>
      <button class="hermes-voice-btn">按住说话</button>
    </div>

    <!-- ===== Inbox 收件箱 ===== -->
    <div v-else-if="view === 'inbox'" class="hermes-page">
      <div class="hermes-subtitle">Hermes 发来的短消息, 只读查看</div>
      <div class="hermes-inbox-list">
        <button
          v-for="(item, i) in INBOX"
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
    <div v-else class="hermes-page">
      <div class="hermes-detail-time">{{ selected.time }}</div>
      <div class="hermes-detail-card">{{ selected.text }}</div>
      <div class="hermes-hint">右滑返回收件箱</div>
    </div>
  </div>
</template>
