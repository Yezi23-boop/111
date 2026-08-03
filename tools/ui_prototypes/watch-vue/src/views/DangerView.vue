<script setup>
// 危险提醒页(一比一对应 danger_detection_view.c)。
import { ref } from 'vue'

const emit = defineEmits(['back'])
const monitoring = ref(true)
const sensitivity = ref(1) // 0保守 1标准 2敏感(默认标准)
const micTested = ref(false)
const alertVisible = ref(false)

const SENS = ['保守', '标准', '敏感']
</script>

<template>
  <div class="danger panel">
    <button class="danger-back" @click="emit('back')">&lt;</button>

    <!-- 状态区 -->
    <div class="danger-status">{{ monitoring ? '正在监听' : '未开启' }}</div>
    <div class="danger-category">CURRENT: NONE</div>
    <div class="danger-result">DANGER</div>

    <!-- 灵敏度 -->
    <div class="danger-sens-title">灵敏度 · 日常推荐</div>
    <div class="danger-sens-row">
      <button
        v-for="(s, i) in SENS"
        :key="s"
        class="danger-sens-btn"
        :class="{ on: sensitivity === i }"
        @click="sensitivity = i"
      >
        {{ s }}
      </button>
    </div>

    <!-- 安全监听开关 -->
    <div class="danger-mon-row">
      <span class="danger-mon-label">安全监听</span>
      <button
        class="danger-switch"
        :class="{ on: monitoring }"
        @click="monitoring = !monitoring"
      >
        <span class="danger-knob"></span>
      </button>
    </div>

    <!-- 麦克风测试 -->
    <div class="danger-mic-row">
      <button class="danger-mic-btn" @click="micTested = true">测麦克风</button>
      <span class="danger-mic-status">{{ micTested ? '已测试' : '未测试' }}</span>
    </div>

    <!-- 置信度卡 -->
    <div class="danger-scores">
      <div class="danger-score-title horn">HORN</div>
      <div class="danger-score-val horn">18.0%</div>
      <div class="danger-score-title siren">SIREN</div>
      <div class="danger-score-val siren">7.0%</div>
    </div>

    <!-- 报警遮罩(全屏,默认隐藏) -->
    <div v-if="alertVisible" class="danger-alert">
      <div class="danger-alert-badge">
        <span class="danger-alert-icon">!</span>
      </div>
    </div>
  </div>
</template>
