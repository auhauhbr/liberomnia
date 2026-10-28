import { fileURLToPath, URL } from 'node:url'
import { loadEnv } from 'vite'
import vue from '@vitejs/plugin-vue'
import { defineConfig } from 'vitest/config'

export default defineConfig(({ mode }) => {
  const root = fileURLToPath(new URL('..', import.meta.url))
  const env = loadEnv(mode, root, 'LIBEROMNIA_')
  const address = env.LIBEROMNIA_ADDRESS || '127.0.0.1'
  const host = address === '0.0.0.0' ? '127.0.0.1' : address === '::' ? '[::1]'
    : address.includes(':') ? `[${address}]` : address
  const target = `http://${host}:${env.LIBEROMNIA_PORT || '8080'}`
  return {
    plugins: [vue()],
    server: { host: 'localhost', port: 5173, strictPort: true, proxy: { '/api': { target } } },
    preview: { host: 'localhost', port: 4173, strictPort: true, proxy: { '/api': { target } } },
    test: { environment: 'jsdom', setupFiles: ['./tests/setup.ts'], restoreMocks: true },
  }
})
