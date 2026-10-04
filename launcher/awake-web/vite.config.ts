import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';

export default defineConfig({
  plugins: [vue()],
  base: './',
  build: {
    outDir: process.env.AWAKE_WEB_OUT_DIR || 'dist',
    emptyOutDir: true,
    assetsInlineLimit: 0,
    target: 'chrome110',
    modulePreload: { polyfill: false },
    sourcemap: false,
  },
});
