<script setup>
// 日历页(一比一对应 emissive_calendar_view.c):
//   背景 #f8fbff 渐变;月份标题顶部;网格 dot 42px 圆,高亮:今天蓝 #007aff/日程绿 #18b957
import { ref, computed } from 'vue'

const emit = defineEmits(['back'])
const month = ref(9)
const year = ref(2026)

const DAYS = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
const NAMES = ['一月', '二月', '三月', '四月', '五月', '六月', '七月', '八月', '九月', '十月', '十一月', '十二月']

// 2026 年 9 月起每月 1 号的星期(周一=0)
const firstDay = computed(() => {
  let base = 0 // 2026-01-01 是周四(周一=0 时为 3)
  const offsets = [0, 31, 59, 90, 120, 151, 181, 212, 243]
  base = (3 + offsets[month.value - 1]) % 7
  return base
})

const daysInMonth = computed(() => DAYS[month.value - 1])
const gridCells = computed(() => {
  const cells = []
  const fd = firstDay.value
  const dm = daysInMonth.value
  for (let i = 0; i < fd; i++) cells.push({ day: null })
  for (let d = 1; d <= dm; d++) {
    // 2026-09:15 节日高亮演示
    cells.push({ day: d, marked: (month.value === 9 && d === 15), today: (month.value === 9 && d === 9) })
  }
  return cells
})

function prev() {
  month.value -= 1
  if (month.value < 1) { month.value = 12; year.value -= 1 }
}
function next() {
  month.value += 1
  if (month.value > 12) { month.value = 1; year.value += 1 }
}
</script>

<template>
  <div class="cal panel">
    <button class="cal-back" @click="emit('back')">‹ 返回</button>
    <div class="cal-title">{{ NAMES[month - 1] }} {{ year }}</div>
    <div class="cal-nav">
      <button class="cal-arrow left" @click="prev">‹</button>
      <button class="cal-arrow right" @click="next">›</button>
    </div>
    <div class="cal-grid">
      <div
        v-for="(c, i) in gridCells"
        :key="i"
        class="cal-cell"
        :class="{ marked: c.marked, today: c.today, blank: !c.day }"
      >
        {{ c.day || '' }}
      </div>
    </div>
  </div>
</template>
