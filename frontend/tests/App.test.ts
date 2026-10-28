import { flushPromises, mount } from '@vue/test-utils'
import { describe, expect, it, vi } from 'vitest'
import App from '../src/App.vue'
import SourceCatalog from '../src/components/SourceCatalog.vue'
import { catalog, health, jsonResponse } from './fixtures'

function mockApi() {
  const fetch = vi.fn(async (path: string) => jsonResponse(path === '/api/health' ? health : catalog))
  vi.stubGlobal('fetch', fetch)
  return fetch
}

describe('home page', () => {
  it('renders a working application shell and a clearly unavailable search', async () => {
    const fetch = mockApi()
    const wrapper = mount(App)
    expect(wrapper.text()).toContain('Carregando catálogo de fontes')
    expect(wrapper.text()).toContain('Conectando à API')
    expect(wrapper.find('h1').text()).toContain('O conhecimento não cabe')
    await flushPromises()
    expect(wrapper.text()).toContain('API conectada')
    expect(wrapper.findAll('.source-card')).toHaveLength(2)
    expect(wrapper.text()).toContain('Gallica')
    expect(wrapper.text()).toContain("Anna's Archive")
    expect(wrapper.text()).toContain('Integração externa')
    expect(wrapper.find<HTMLButtonElement>('button[type="submit"]').element.disabled).toBe(true)
    expect(wrapper.text()).toContain('Pesquisa temporariamente indisponível')
    expect(wrapper.findAll<HTMLInputElement>('input[type="checkbox"]').every(input => input.element.disabled)).toBe(true)
    await wrapper.find('form').trigger('submit')
    expect(fetch.mock.calls.map(([path]) => path)).toEqual(['/api/health', '/api/sources'])
  })

  it('applies historical periods, accepts modes and detects invalid year ranges', async () => {
    mockApi()
    const wrapper = mount(App)
    await flushPromises()
    await wrapper.find('#period').setValue('nineteenth')
    expect(wrapper.find<HTMLInputElement>('#year-from').element.value).toBe('1801')
    expect(wrapper.find<HTMLInputElement>('#year-to').element.value).toBe('1900')
    await wrapper.find('input[value="author"]').setValue()
    expect(wrapper.find<HTMLInputElement>('input[value="author"]').element.checked).toBe(true)
    await wrapper.find('#language').setValue('pt')
    await wrapper.find('#year-from').setValue('1901')
    expect(wrapper.find<HTMLSelectElement>('#period').element.value).toBe('custom')
    expect(wrapper.find('#range-error').text()).toContain('ano inicial')
    await wrapper.find('#year-to').setValue('2000')
    expect(wrapper.find('#range-error').exists()).toBe(false)
    await wrapper.find('#year-from').setValue('0')
    expect(wrapper.find('#range-error').text()).toContain('entre 1 e 9999')
    await wrapper.find('#period').setValue('any')
    expect(wrapper.find<HTMLInputElement>('#year-from').element.value).toBe('')
    expect(wrapper.find<HTMLInputElement>('#year-to').element.value).toBe('')
  })

  it('shows connection failures and recovers on retry', async () => {
    const fetch = vi.fn().mockRejectedValue(new TypeError('Failed to fetch'))
    vi.stubGlobal('fetch', fetch)
    const wrapper = mount(App)
    await flushPromises()
    expect(wrapper.text()).toContain('API indisponível')
    expect(wrapper.text()).toContain('Não foi possível conectar à API')
    fetch.mockImplementation(async (path: string) => jsonResponse(path === '/api/health' ? health : catalog))
    await wrapper.find('.state-box button').trigger('click')
    await flushPromises()
    expect(wrapper.text()).toContain('API conectada')
    expect(wrapper.findAll('.source-card')).toHaveLength(2)
    expect(wrapper.find('.connection-error').exists()).toBe(false)
  })

  it('keeps health and source failures independent', async () => {
    vi.stubGlobal('fetch', vi.fn(async (path: string) => path === '/api/health'
      ? jsonResponse(health) : jsonResponse({}, 503)))
    const wrapper = mount(App)
    await flushPromises()
    expect(wrapper.text()).toContain('API conectada')
    expect(wrapper.text()).toContain('HTTP 503')
    expect(wrapper.findAll('.source-card')).toHaveLength(0)
  })

  it('cancels pending requests on unmount', () => {
    const signals: AbortSignal[] = []
    vi.stubGlobal('fetch', vi.fn((_path: string, options: RequestInit) => {
      signals.push(options.signal as AbortSignal)
      return new Promise<Response>((_resolve, reject) => {
        options.signal?.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')))
      })
    }))
    const wrapper = mount(App)
    wrapper.unmount()
    expect(signals).toHaveLength(2)
    expect(signals.every(signal => signal.aborted)).toBe(true)
  })

  it('does not let an old request replace the result of a retry', async () => {
    let resolveOld: ((response: Response) => void) | undefined
    let sourceRequests = 0
    vi.stubGlobal('fetch', vi.fn((path: string) => {
      if (path === '/api/health') return Promise.resolve(jsonResponse({}, 503))
      sourceRequests += 1
      if (sourceRequests === 1) return new Promise<Response>(resolve => { resolveOld = resolve })
      return Promise.resolve(jsonResponse(catalog))
    }))
    const wrapper = mount(App)
    await flushPromises()
    await wrapper.find('.connection-error button').trigger('click')
    await flushPromises()
    resolveOld?.(jsonResponse({ ...catalog, sources: [] }))
    await flushPromises()
    expect(wrapper.findAll('.source-card')).toHaveLength(2)
  })
})

describe('source catalog', () => {
  it('distinguishes available sources and confirmed capabilities without interpreting HTML', () => {
    const source = catalog.sources[0]!
    const wrapper = mount(SourceCatalog, { props: { state: { status: 'success', data: {
      ...catalog, sources: [{ ...source, name: '<img src=x onerror=alert(1)>', implementationStatus: 'available',
        capabilities: [{ id: 'title', status: 'confirmed' }] }],
    } } } })
    expect(wrapper.text()).toContain('Disponível')
    expect(wrapper.text()).toContain('confirmada')
    expect(wrapper.text()).toContain('<img src=x onerror=alert(1)>')
    expect(wrapper.find('img').exists()).toBe(false)
  })

  it('shows an empty catalog without inventing sources', () => {
    const wrapper = mount(SourceCatalog, { props: { state: {
      status: 'success', data: { ...catalog, sources: [] },
    } } })
    expect(wrapper.text()).toContain('Nenhuma fonte cadastrada')
    expect(wrapper.findAll('.source-card')).toHaveLength(0)
  })
})
