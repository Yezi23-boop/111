<script setup>
import { ref } from 'vue'

const emit = defineEmits(['back', 'open'])
const selected = ref('')

const GAMES = [
  { id: '2048', label: '2048 益智拼图', icon: '▣', tone: 'blue' },
  { id: 'flappy', label: '飞翔的小鸟 (Flappy)', icon: '♫', tone: 'sky' },
  { id: 'dino', label: '小恐龙快跑 (Runner)', icon: '⚙', tone: 'amber' },
]
</script>

<template>
  <div class="games panel">
    <div class="games-title">精选小游戏</div>
    <div class="games-subtitle">请选择一款小游戏开始游玩:</div>

    <div class="games-list">
      <button
        v-for="game in GAMES"
        :key="game.id"
        class="game-entry"
        :class="{ selected: selected === game.id }"
        @click="selected = game.id; emit('open', game.id)"
      >
        <span class="game-icon" :class="game.tone" aria-hidden="true">{{ game.icon }}</span>
        <span class="game-name">{{ game.label }}</span>
      </button>
    </div>

    <button class="games-back" @click="emit('back')"><span aria-hidden="true">‹</span>返回主菜单</button>
  </div>
</template>
