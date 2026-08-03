// 手势组合式函数:按板端 events_init.c 的判定规格实现
//   水平滑动: |dx| >= 70px 且 |dx| > |dy|*2  -> horizontal
//   竖直滑动: |dy| >= 70px 且 |dy| > |dx|*2  -> vertical
//   位移 <= 12px 判定为点击(板端入口防抖阈值)
export function useSwipe(onSwipe, onTap) {
  let sx = 0
  let sy = 0
  let tracking = false

  function down(e) {
    tracking = true
    sx = e.clientX
    sy = e.clientY
  }

  function up(e) {
    if (!tracking) return
    tracking = false
    const dx = e.clientX - sx
    const dy = e.clientY - sy
    const adx = Math.abs(dx)
    const ady = Math.abs(dy)

    if (adx <= 12 && ady <= 12) {
      if (onTap) onTap(e)
      return
    }
    if (adx >= 70 && adx > ady * 2) {
      if (onSwipe) onSwipe({ dx, dy })
    } else if (ady >= 70 && ady > adx * 2) {
      if (onSwipe) onSwipe({ dx, dy })
    }
  }

  function cancel() {
    tracking = false
  }

  return { down, up, cancel }
}
