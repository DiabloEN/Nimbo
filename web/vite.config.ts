import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';

export default defineConfig({
  plugins: [vue()],
  build: { license: { fileName: 'assets/licenses.md' } },
  server: {
    host: '127.0.0.1', port: 5173, strictPort: true,
    proxy: { '/api': {
      target: 'http://127.0.0.1:8090', changeOrigin: true,
      // The dev proxy is local-only. The API still enforces its own exact Origin.
      headers: { Origin: 'http://127.0.0.1:8090' },
    } },
  },
});
