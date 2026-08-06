<script setup>
import { computed, ref } from 'vue'

const props = defineProps({
  game: { type: String, required: true },
})
const emit = defineEmits(['back'])
const paused = ref(false)
const started = ref(false)
const restartKey = ref(0)

const is2048 = computed(() => props.game === '2048')
const isFlappy = computed(() => props.game === 'flappy')

function restart() {
  restartKey.value += 1
  paused.value = false
  started.value = false
}

function togglePause() {
  paused.value = !paused.value
}
</script>

<template>
  <div class="game-page panel" :class="`game-page-${game}`" :key="restartKey">
    <template v-if="is2048">
      <div class="game-score-card score-card-score"><span>得分</span><strong>0</strong></div>
      <div class="game-score-card score-card-best"><span>最高</span><strong>0</strong></div>

      <div class="game-2048-board" aria-label="2048 棋盘">
        <div
          v-for="(value, index) in [0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0]"
          :key="index"
          class="game-2048-tile"
          :class="{ filled: value }"
        >{{ value || '' }}</div>
      </div>

      <div class="game-controls">
        <button class="game-control game-control-light" @click="emit('back')"><b>‹</b>返回</button>
        <button class="game-control game-control-green" @click="restart"><b>＋</b>新游戏</button>
        <button class="game-control game-control-light" @click="togglePause">
          <b>{{ paused ? '▶' : 'Ⅱ' }}</b>{{ paused ? '继续' : '暂停' }}
        </button>
      </div>
    </template>

    <template v-else-if="isFlappy">
      <div class="game-score-card game-score-wide"><span>分数</span><strong>0</strong><em>最高</em><strong>0</strong></div>
      <div class="flappy-bird" :class="{ flying: started }" aria-hidden="true"><i></i></div>
      <div v-if="!started" class="game-start-hint flappy-hint">轻触屏幕开始飞翔</div>
      <button class="game-stage-hit" aria-label="开始飞翔" @click="started = true"></button>
      <div class="game-controls">
        <button class="game-control game-control-light" @click="emit('back')"><b>‹</b>退出</button>
        <button class="game-control game-control-blue" @click="restart"><b>＋</b>重开</button>
        <button class="game-control game-control-light" @click="togglePause">
          <b>{{ paused ? '▶' : 'Ⅱ' }}</b>{{ paused ? '继续' : '暂停' }}
        </button>
      </div>
    </template>

    <template v-else>
      <div class="game-score-card game-score-wide"><span>分数</span><strong>0</strong><em>最高</em><strong>0</strong></div>
      <div class="dino-ground"></div>
      <div class="dino-sprite" :class="{ started }" aria-hidden="true"><i></i></div>
      <div v-if="!started" class="game-start-hint dino-hint">右屏轻触开始<br>左屏按住下蹲</div>
      <button class="game-stage-hit" aria-label="开始小恐龙" @click="started = true"></button>
      <div class="dino-dust dust-one"></div><div class="dino-dust dust-two"></div>
      <div class="game-controls">
        <button class="game-control game-control-light" @click="emit('back')"><b>‹</b>退出</button>
        <button class="game-control game-control-amber" @click="restart"><b>＋</b>重开</button>
        <button class="game-control game-control-light" @click="togglePause">
          <b>{{ paused ? '▶' : 'Ⅱ' }}</b>{{ paused ? '继续' : '暂停' }}
        </button>
      </div>
    </template>
  </div>
</template>
