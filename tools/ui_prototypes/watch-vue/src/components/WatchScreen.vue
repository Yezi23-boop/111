<script setup>
// 410×502 手表画布外壳:与真机 1:1,四角圆角遮罩在全局样式中
// theme='dark' 时套用深色(音乐页)外壳,默认浅色主屏外壳
defineProps({
  theme: { type: String, default: '' },
})
</script>

<template>
  <div class="watch" :class="theme ? 'watch-' + theme : ''">
    <slot />
  </div>
</template>
