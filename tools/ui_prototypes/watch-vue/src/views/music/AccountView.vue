<script setup>
// 音乐账号页:扫码入口与账号说明
import TopBar from '../../components/TopBar.vue'

const emit = defineEmits(['back'])

</script>

<template>
  <div class="music-page music-page--account">
    <TopBar title="账号" :show-account="false" @back="emit('back')" />
    <div class="account-content">
      <div class="section-kicker">CLOUD LIBRARY</div>
      <h1>连接你的音乐</h1>
      <p class="account-intro">扫码登录网易云音乐，收藏和歌单会在这里出现。</p>
    </div>

    <div class="qr-card">
      <div class="qr-frame">
        <i
          v-for="(black, i) in QR_CELLS"
          :key="i"
          :style="{ background: black ? '#17241e' : '#edf4ee' }"
        />
      </div>
      <div class="account-hint">打开手机 App 扫一扫</div>
    </div>

    <div class="account-footer">二维码仅用于登录，不保存账号密码</div>
  </div>
</template>
