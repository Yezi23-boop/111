<script setup>
// 通知中心对应 generated/screens/gg_Notifications.c。
const emit = defineEmits(['back', 'open-detail'])

const notifications = [
  { time: '今天 16:20', text: 'Hermes：下午三点取快递', unread: true },
  { time: '今天 11:42', text: '电池日志：晚饭后查看', unread: true },
  { time: '昨天 22:08', text: 'UI 优化方向已记录', unread: false },
  { time: '周一 09:10', text: '系统提示：新版本可用', unread: false },
]
</script>

<template>
  <div class="nc panel">
    <div class="nc-root">
      <div class="nc-badge">2 未读</div>
      <div class="nc-title">通知中心</div>
      <button class="nc-back" aria-label="返回" @click="emit('back')">&lt;</button>

      <section class="nc-page" aria-label="最近通知">
        <div class="nc-subtitle">最近通知 · 固定槽位，可由业务填充</div>
        <div class="nc-list">
          <button
            v-for="(item, index) in notifications"
            :key="item.time"
            class="nc-row"
            :class="{ unread: item.unread }"
            @click="index === 0 && emit('open-detail')"
          >
            <span v-if="item.unread" class="nc-dot" aria-hidden="true"></span>
            <span class="nc-row-time">{{ item.time }}</span>
            <span class="nc-row-text">{{ item.text }}</span>
          </button>
        </div>
      </section>
    </div>
  </div>
</template>
