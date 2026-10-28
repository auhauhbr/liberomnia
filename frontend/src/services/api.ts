import type { HealthResponse, Source, SourceCapability, SourcesResponse } from '../types/api'

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}

function isNonemptyString(value: unknown): value is string {
  return typeof value === 'string' && value.trim().length > 0
}

function isCapability(value: unknown): value is SourceCapability {
  return isObject(value) && isNonemptyString(value.id)
    && (value.status === 'planned' || value.status === 'confirmed')
}

function isSource(value: unknown): value is Source {
  return isObject(value) && isNonemptyString(value.id) && isNonemptyString(value.name)
    && (value.integrationType === 'api' || value.integrationType === 'external')
    && (value.implementationStatus === 'planned' || value.implementationStatus === 'available')
    && typeof value.notes === 'string'
    && Array.isArray(value.capabilities) && value.capabilities.every(isCapability)
}

async function getJson(path: string, signal: AbortSignal): Promise<unknown> {
  const controller = new AbortController()
  const cancel = () => controller.abort()
  signal.addEventListener('abort', cancel, { once: true })
  if (signal.aborted) controller.abort()
  const timeout = setTimeout(cancel, 8000)
  try {
    const response = await fetch(path, { signal: controller.signal, headers: { Accept: 'application/json' } })
    if (!response.ok) throw new Error(`A API respondeu com HTTP ${response.status}.`)
    return await response.json() as unknown
  } catch (error) {
    if (controller.signal.aborted && !signal.aborted) {
      throw new Error('A API demorou a responder. Tente novamente.')
    }
    throw error
  } finally {
    clearTimeout(timeout)
    signal.removeEventListener('abort', cancel)
  }
}

export async function getHealth(signal: AbortSignal): Promise<HealthResponse> {
  const value = await getJson('/api/health', signal)
  if (!isObject(value) || value.apiVersion !== '1' || value.application !== 'LiberOmnia' || value.status !== 'ok') {
    throw new Error('A API retornou um estado de saúde incompatível.')
  }
  return { apiVersion: '1', application: 'LiberOmnia', status: 'ok' }
}

export async function getSources(signal: AbortSignal): Promise<SourcesResponse> {
  const value = await getJson('/api/sources', signal)
  if (!isObject(value) || value.apiVersion !== '1' || typeof value.searchAvailable !== 'boolean'
    || !Array.isArray(value.sources) || !value.sources.every(isSource)
    || new Set(value.sources.map(source => source.id)).size !== value.sources.length) {
    throw new Error('A API retornou um catálogo de fontes incompatível.')
  }
  return { apiVersion: '1', searchAvailable: value.searchAvailable, sources: value.sources }
}
