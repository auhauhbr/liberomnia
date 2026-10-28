import { describe, expect, it, vi } from 'vitest'
import { getHealth, getSources } from '../src/services/api'
import { catalog, health, jsonResponse } from './fixtures'

describe('API contracts', () => {
  it('accepts versioned responses and additive fields', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValueOnce(jsonResponse({ ...health, extra: true }))
      .mockResolvedValueOnce(jsonResponse({ ...catalog, extra: true })))
    const signal = new AbortController().signal
    expect(await getHealth(signal)).toEqual(health)
    expect(await getSources(signal)).toEqual(catalog)
  })

  it.each([
    {}, { ...catalog, apiVersion: '2' }, { ...catalog, searchAvailable: 'false' },
    { ...catalog, sources: [catalog.sources[0], catalog.sources[0]] },
    { ...catalog, sources: [{ ...catalog.sources[0], capabilities: ['title'] }] },
    { ...catalog, sources: [{ ...catalog.sources[0], implementationStatus: 'operational' }] },
  ])('rejects malformed or unsupported catalogs %#', async (body) => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(jsonResponse(body)))
    await expect(getSources(new AbortController().signal)).rejects.toThrow('catálogo de fontes incompatível')
  })

  it('rejects a malformed health response', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(jsonResponse({ ...health, status: 'broken' })))
    await expect(getHealth(new AbortController().signal)).rejects.toThrow('estado de saúde incompatível')
  })

  it('reports HTTP errors', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(jsonResponse({}, 502)))
    await expect(getHealth(new AbortController().signal)).rejects.toThrow('HTTP 502')
  })

  it('rejects invalid JSON', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('<html>unavailable</html>')))
    await expect(getSources(new AbortController().signal)).rejects.toThrow()
  })

  it('times out a hanging connection', async () => {
    vi.useFakeTimers()
    vi.stubGlobal('fetch', vi.fn((_path: string, options: RequestInit) => new Promise((_resolve, reject) => {
      options.signal?.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')))
    })))
    const result = expect(getHealth(new AbortController().signal)).rejects.toThrow('demorou a responder')
    await vi.advanceTimersByTimeAsync(8000)
    await result
  })
})
