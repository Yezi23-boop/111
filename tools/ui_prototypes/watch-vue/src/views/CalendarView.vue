<script setup>
// 日历页(一比一对应 emissive_calendar_view.c):
//   背景 #f8fbff 渐变;月份标题顶部;网格 dot 42px 圆,高亮:今天蓝 #007aff/日程绿 #18b957
import { ref, computed } from 'vue'

const emit = defineEmits(['back'])
const month = ref(6)
const year = ref(2025)
const selectedDay = ref(18)

const NAMES = ['一月', '二月', '三月', '四月', '五月', '六月', '七月', '八月', '九月', '十月', '十一月', '十二月']

const firstDay = computed(() => {
  const sundayBased = new Date(year.value, month.value - 1, 1).getDay()
  return (sundayBased + 6) % 7
})

const daysInMonth = computed(() => new Date(year.value, month.value, 0).getDate())
const gridCells = computed(() => {
  const cells = []
  const fd = firstDay.value
  const dm = daysInMonth.value
  for (let i = 0; i < fd; i++) cells.push({ day: null })
  for (let d = 1; d <= dm; d++) {
    cells.push({ day: d, selected: d === selectedDay.value })
  }
  return cells
})

let startX = 0
let startY = 0
let tracking = false
function calendarDown(e) {
  startX = e.clientX
  startY = e.clientY
  tracking = true
}
function calendarUp(e) {
  if (!tracking) return
  tracking = false
  const dx = e.clientX - startX
  const dy = e.clientY - startY
  if (dx >= 60 && Math.abs(dy) <= 50) emit('back')
}
function calendarCancel() {
  tracking = false
}

function prev() {
  month.value -= 1
  if (month.value < 1) { month.value = 12; year.value -= 1 }
  selectedDay.value = Math.min(selectedDay.value, daysInMonth.value)
}
function next() {
  month.value += 1
  if (month.value > 12) { month.value = 1; year.value += 1 }
  selectedDay.value = Math.min(selectedDay.value, daysInMonth.value)
}
</script>

<template>
  <div
    class="cal panel"
    @pointerdown="calendarDown"
    @pointerup="calendarUp"
    @pointercancel="calendarCancel"
  >
    <div class="cal-title">{{ NAMES[month - 1] }}</div>
    <div class="cal-nav">
      <button class="cal-arrow left" @click="prev">‹</button>
      <button class="cal-arrow right" @click="next">›</button>
    </div>
    <div class="cal-weekdays">
      <span v-for="day in ['一', '二', '三', '四', '五', '六', '日']" :key="day">{{ day }}</span>
    </div>
    <div class="cal-grid" :class="gridCells.length > 35 ? 'rows-6' : 'rows-5'">
      <div
        v-for="(c, i) in gridCells"
        :key="i"
        class="cal-cell"
        :class="{ selected: c.selected, blank: !c.day }"
        @click="c.day && (selectedDay = c.day)"
      >
        {{ c.day || '' }}
      </div>
    </div>
  </div>
</template>
