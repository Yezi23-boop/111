<script setup>
// Wi-Fi 管理页(一比一对应 wifi_management_controller.c):
//   标题 40,28;关闭 324,20 44×44;状态面板 40,84 330×94;
//   Bento 按钮:蓝牙配网 40,192 160×120 蓝/网页配网 210,192 160×120 紫;
//   断开连接 40,394 330×48 红
import { ref } from 'vue'

const emit = defineEmits(['back'])
const status = ref({ text: '未连接', detail: 'Wi-Fi 未连接', connected: false })
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
    </div>

    <!-- Bento 配网按钮 -->
    <button class="wifi-bento" style="left: 40px; background: #0a84ff" @click="message = '蓝牙配网启动中...'">
      <span class="wifi-bento-circle">🔵</span>
      <span class="wifi-bento-title">蓝牙配网</span>
    </button>
    <button class="wifi-bento" style="left: 210px; background: #bf5af2" @click="message = '网页配网启动中...'">
      <span class="wifi-bento-circle">📶</span>
      <span class="wifi-bento-title">网页配网</span>
    </button>

    <!-- 长条操作按钮 -->
    <button class="wifi-action" style="top: 330px" @click="message = '重试已保存网络...'">
      重试已保存网络
    </button>
    <button class="wifi-action" style="top: 394px; color: #ff453a; border-color: #ff453a" @click="message = '正在断开连接...'">
      断开连接
    </button>

    <!-- 内联提示 -->
    <div v-if="message" class="wifi-msg">{{ message }}</div>
  </div>
</template>
