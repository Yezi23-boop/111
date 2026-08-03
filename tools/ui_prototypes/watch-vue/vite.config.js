import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// Vite 配置:标准 Vue 单文件组件支持;端口固定 8766,与 HTML 版(8765)错开
export default defineConfig({
  plugins: [vue()],
  server: {
    port: 8767,
    strictPort: true,
  },
})
