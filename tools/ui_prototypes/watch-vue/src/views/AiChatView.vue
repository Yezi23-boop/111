<script setup>
// 小智 AI 聊天页(一比一对应 ai_chat_view.c):
//   背景 #fbfbfa;聊天区 40,28 330×332;气泡:用户右 #e1f3fe / AI 左白 #ffffff 边 #eaeaea;
//   语音按钮 55,384 300×54 黑,按住变红 #b3261e;footer 40,446 副按钮
import { ref } from 'vue'

const emit = defineEmits(['back'])
const pressing = ref(false)

const badge = ref({ text: '已联网', cls: 'ok' }) // ok | err | warn

const MESSAGES = [
  { kind: 'ai', text: '你好,我是小智。有什么可以帮你?' },
  { kind: 'user', text: '帮我定一个明早 8 点的闹钟' },
  { kind: 'ai', text: '好的,已设置明早 08:00 的闹钟。需要我同时提醒你带伞吗?(今天有雨)' },
]
</script>

<template>
  <div class="ai-chat panel">
    <!-- 顶部:返回 + 标题 + 状态徽章(controller 动态管理) -->
    <button class="ai-back" @click="emit('back')">‹</button>
    <div class="ai-title">小智</div>
    <div class="ai-badge" :class="badge.cls">{{ badge.text }}</div>

    <!-- 聊天卡片区 -->
    <div class="ai-card">
      <div class="ai-scroll">
        <template v-for="(m, i) in MESSAGES" :key="i">
          <div class="ai-row">
            <!-- 状态/系统提示 -->
            <div v-if="m.kind === 'sys'" class="ai-sys">{{ m.text }}</div>
            <!-- 用户气泡:右对齐蓝底 -->
            <div v-else-if="m.kind === 'user'" class="ai-bubble user">{{ m.text }}</div>
            <!-- AI 气泡:左对齐白底 -->
            <div v-else class="ai-bubble ai">{{ m.text }}</div>
          </div>
        </template>
      </div>
    </div>

    <!-- 语音按钮 -->
    <button
      class="ai-voice"
      :class="{ pressing }"
      @pointerdown="pressing = true"
      @pointerup="pressing = false"
      @pointerleave="pressing = false"
    >
      {{ pressing ? '松开发送' : '按住说话' }}
    </button>

    <!-- footer 副按钮(由 controller 控制显隐) -->
    <div class="ai-footer">
      <button class="ai-secondary">退出</button>
    </div>
  </div>
</template>
