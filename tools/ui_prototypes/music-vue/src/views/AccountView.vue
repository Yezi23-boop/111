<script setup>
// 扫码登录页:顶部栏 + 状态文案 + 伪二维码卡片
// 坐标对齐板端:状态 40,82 330×32;二维码卡片 95,132 220 宽
import TopBar from '../components/TopBar.vue'

const emit = defineEmits(['back'])

// 21×21 伪二维码格:白色定位角 + 随机黑格(纯占位,换真码时接 canvas)
const QR_CELLS = Array.from({ length: 21 * 21 }, (_, i) => {
  const r = Math.floor(i / 21)
  const c = i % 21
  const corner = (r < 7 && c < 7) || (r < 7 && c > 13) || (r > 13 && c < 7)
  if (corner) return i % 17 !== 0 && i % 23 !== 0
  return i % 3 === 0
})
</script>

<template>
  <TopBar title="登录" :show-account="false" @back="emit('back')" />

  <div class="account-status">扫码登录云端音乐账号</div>
  <div class="qr-card">
    <div class="qr">
      <i
        v-for="(black, i) in QR_CELLS"
        :key="i"
        :style="{ background: black ? '#111' : '#fff' }"
      />
    </div>
    <div class="hint">打开手机 App<br />扫一扫完成登录</div>
  </div>
</template>
