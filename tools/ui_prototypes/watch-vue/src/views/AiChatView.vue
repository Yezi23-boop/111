<script setup>
// 小智 AI 聊天页(一比一对应 ai_chat_view.c):
//   背景 #fbfbfa;聊天区 40,28 330×332;气泡:用户右 #e1f3fe / AI 左白 #ffffff 边 #eaeaea;
//   语音按钮 55,384 300×54 黑,按住变红 #b3261e;footer 40,446 副按钮
import { ref } from 'vue'

const emit = defineEmits(['back'])
const pressing = ref(false)

const MESSAGES = [
  { kind: 'user', text: '你好, 小智!' },
  { kind: 'ai', text: '你好! 我是小智, 你的个人 AI 语音助手. 很高兴为你服务.' },
  { kind: 'user', text: '今天天气怎么样?' },
  { kind: 'ai', text: '今天北京天气晴朗, 气温 18 到 28 度, 非常适合户外出行. 建议注意防晒.' },
]
</script>

<template>
  <div class="ai-chat panel">
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
      @pointercancel="pressing = false"
    >
      {{ pressing ? '松开发送' : '按住说话' }}
    </button>

    <!-- footer 副按钮(由 controller 控制显隐) -->
    <div class="ai-footer">
      <button class="ai-secondary" @click="emit('back')">返回主页</button>
    </div>
  </div>
</template>
