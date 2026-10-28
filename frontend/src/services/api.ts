import type { HealthResponse, Source, SourceCapability, SourcesResponse } from '../types/api'

export function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}

export function isNonemptyString(value: unknown): value is string {
  return typeof value === 'string' && value.trim().length > 0
}

function isCapability(value: unknown): value is SourceCapability {
  return isObject(value) && isNonemptyString(value.id)
    && (value.status === 'planned' || value.status === 'confirmed')
}

function isFilterSupport(value: unknown): boolean {
  if (!isObject(value)) return false
  return ['publication', 'language', 'availability'].every(key => {
    const support = value[key]
    return support === 'native' || support === 'local' || support === 'unsupported'
  })
}

function isSource(value: unknown): value is Source {
  return isObject(value) && isNonemptyString(value.id) && isNonemptyString(value.name)
    && (value.integrationType === 'api' || value.integrationType === 'external')
    && (value.implementationStatus === 'planned' || value.implementationStatus === 'available' || value.implementationStatus === 'temporarily_unavailable')
    && (value.filterSupport === undefined || isFilterSupport(value.filterSupport))
    && typeof value.notes === 'string'
    && Array.isArray(value.capabilities) && value.capabilities.every(isCapability)
}

export class ApiError extends Error {
  constructor(public readonly status: number, public readonly code: string, message: string) {
    super(message)
    this.name = 'ApiError'
  }
}

export async function requestJson(path: string, signal: AbortSignal, method: 'GET' | 'POST' = 'GET', body?: unknown): Promise<unknown> {
  const controller = new AbortController()
  const cancel = () => controller.abort()
  signal.addEventListener('abort', cancel, { once: true })
  if (signal.aborted) controller.abort()
  const timeout = setTimeout(cancel, 8000)
  try {
    const response = await fetch(path, {
      signal: controller.signal, method,
      headers: { Accept: 'application/json', ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
      ...(body === undefined ? {} : { body: JSON.stringify(body) }),
    })
    if (!response.ok) {
      const payload: unknown = await response.json().catch(() => undefined)
      if (isObject(payload) && payload.apiVersion === '1' && isObject(payload.error)
        && isNonemptyString(payload.error.code) && isNonemptyString(payload.error.message)) {
        throw new ApiError(response.status, payload.error.code, payload.error.message)
      }
      throw new ApiError(response.status, 'http_error', `A API respondeu com HTTP ${response.status}.`)
    }
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
  const value = await requestJson('/api/health', signal)
  if (!isObject(value) || value.apiVersion !== '1' || value.application !== 'LiberOmnia' || value.status !== 'ok') {
    throw new Error('A API retornou um estado de saúde incompatível.')
  }
  return { apiVersion: '1', application: 'LiberOmnia', status: 'ok' }
}

export async function getSources(signal: AbortSignal): Promise<SourcesResponse> {
  const value = await requestJson('/api/sources', signal)
  if (!isObject(value) || value.apiVersion !== '1' || typeof value.searchAvailable !== 'boolean'
    || !Array.isArray(value.sources) || !value.sources.every(isSource)
    || new Set(value.sources.map(source => source.id)).size !== value.sources.length) {
    throw new Error('A API retornou um catálogo de fontes incompatível.')
  }
  return { apiVersion: '1', searchAvailable: value.searchAvailable, sources: value.sources }
}
