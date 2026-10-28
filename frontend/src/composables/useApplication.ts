import { onMounted, onUnmounted, shallowRef } from 'vue'
import { getHealth, getSources } from '../services/api'
import type { HealthResponse, LoadState, SourcesResponse } from '../types/api'

function errorMessage(error: unknown): string {
  return error instanceof TypeError ? 'Não foi possível conectar à API. Verifique se o servidor está em execução.'
    : error instanceof Error ? error.message : 'Não foi possível carregar os dados. Tente novamente.'
}

export function useApplication() {
  const health = shallowRef<LoadState<HealthResponse>>({ status: 'loading' })
  const catalog = shallowRef<LoadState<SourcesResponse>>({ status: 'loading' })
  let active: AbortController | undefined

  async function refresh() {
    active?.abort()
    const controller = new AbortController()
    active = controller
    health.value = { status: 'loading' }
    catalog.value = { status: 'loading' }

    async function load<T>(request: (signal: AbortSignal) => Promise<T>, update: (state: LoadState<T>) => void) {
      try {
        const data = await request(controller.signal)
        if (!controller.signal.aborted) update({ status: 'success', data })
      } catch (error) {
        if (!controller.signal.aborted) update({ status: 'error', message: errorMessage(error) })
      }
    }

    await Promise.all([
      load(getHealth, state => { health.value = state }),
      load(getSources, state => { catalog.value = state }),
    ])
  }

  onMounted(() => { void refresh() })
  onUnmounted(() => active?.abort())
  return { health, catalog, refresh }
}
