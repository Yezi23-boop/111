<script setup>
// Wi-Fi 管理页(一比一对应 wifi_management_controller.c):
//   标题 40,28;关闭 324,20 44×44;状态面板 40,84 330×94;
//   Bento 按钮:蓝牙配网 40,192 160×120 蓝/网页配网 210,192 160×120 紫;
//   断开连接 40,394 330×48 红
import { ref } from 'vue'

const emit = defineEmits(['back'])
const status = ref({ text: '已连接', detail: 'IP: 192.168.1.88', connected: true })
const message = ref('')
</script>

<template>
  <div class="wifi panel">
    <div class="wifi-title">Wi-Fi</div>
    <button class="wifi-close" @click="emit('back')">✕</button>

    <!-- 状态面板 -->
    <div class="wifi-status-panel">
      <span class="wifi-status-lbl">状态</span>
      <span class="wifi-dot" :class="{ on: status.connected }"></span>
      <div class="wifi-status-title">{{ status.text }}</div>
      <div class="wifi-status-detail">{{ status.detail }}</div>
      <svg class="wifi-glyph wifi-glyph--status" viewBox="0 0 46 36" aria-hidden="true">
        <path d="M3 10C14-1 32-1 43 10M11 19c7-7 17-7 24 0M19 27c2-2 6-2 8 0" />
        <circle cx="23" cy="32" r="3" />
      </svg>
    </div>

    <!-- Bento 配网按钮 -->
    <button class="wifi-bento" style="left: 40px; --wifi-icon: #0a84ff" @click="message = '蓝牙配网启动中...'">
      <span class="wifi-bento-title">蓝牙配网</span>
      <span class="wifi-bento-circle">
        <svg class="bluetooth-glyph" viewBox="0 0 24 32" aria-hidden="true">
          <path d="M12 1v30l9-8-9-7 9-7-9-8M3 9l17 14M3 23L20 9" />
        </svg>
      </span>
    </button>
    <button class="wifi-bento" style="left: 210px; --wifi-icon: #bf5af2" @click="message = '网页配网启动中...'">
      <span class="wifi-bento-title">网页配网</span>
      <span class="wifi-bento-circle">
        <svg class="wifi-glyph" viewBox="0 0 46 36" aria-hidden="true">
          <path d="M3 10C14-1 32-1 43 10M11 19c7-7 17-7 24 0M19 27c2-2 6-2 8 0" />
          <circle cx="23" cy="32" r="3" />
        </svg>
      </span>
    </button>

    <!-- 长条操作按钮 -->
    <button class="wifi-action" style="top: 326px" @click="message = '重试已保存网络...'">
      <svg class="wifi-glyph wifi-glyph--action" viewBox="0 0 46 36" aria-hidden="true">
        <path d="M3 10C14-1 32-1 43 10M11 19c7-7 17-7 24 0M19 27c2-2 6-2 8 0" />
        <circle cx="23" cy="32" r="3" />
      </svg>
      <span>重试已保存网络</span>
      <span class="wifi-action-arrow" aria-hidden="true">›</span>
    </button>
    <button class="wifi-action wifi-action--disconnect" style="top: 394px" @click="message = '正在断开连接...'">
      <span>断开连接</span>
      <svg class="wifi-power" viewBox="0 0 24 24" aria-hidden="true">
        <path d="M12 2v10M6.7 5.8a8 8 0 1 0 10.6 0" />
      </svg>
    </button>

    <!-- 内联提示 -->
    <div v-if="message" class="wifi-msg">{{ message }}</div>
  </div>
</template>
