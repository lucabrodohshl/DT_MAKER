/// <reference types="vitest/config" />
import { fileURLToPath, URL } from 'node:url';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

// The UI talks only to twin-studio (/api/v1). In development Vite proxies /api to it.
const studio = process.env.TWIN_STUDIO_URL ?? 'http://127.0.0.1:8080';

export default defineConfig({
  plugins: [react()],
  resolve: { alias: { '@': fileURLToPath(new URL('./src', import.meta.url)) } },
  server: {
    port: 5173,
    proxy: { '/api': { target: studio, changeOrigin: false } },
  },
  build: {
    target: 'es2022',
    sourcemap: true,
    chunkSizeWarningLimit: 900,
    rollupOptions: {
      output: {
        // Split heavy libraries into their own cacheable chunks.
        manualChunks(id: string) {
          if (!id.includes('node_modules')) return undefined;
          if (id.includes('@codemirror') || id.includes('@lezer')) return 'editor';
          if (id.includes('@xyflow') || id.includes('dagre')) return 'graph';
          if (id.includes('uplot')) return 'charts';
          if (id.includes('@tanstack')) return 'query';
          if (id.includes('react-router') || id.includes('react-dom') || id.includes('/react/')) return 'react';
          return undefined;
        },
      },
    },
  },
  test: {
    environment: 'jsdom',
    globals: true,
    setupFiles: ['./src/test/setup.ts'],
    css: false,
    include: ['src/**/*.test.{ts,tsx}'],
  },
});
