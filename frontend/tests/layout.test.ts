import { flushPromises, mount } from '@vue/test-utils'
import { describe, expect, it, vi } from 'vitest'
import App from '../src/App.vue'
import { catalog, health, jsonResponse } from './fixtures'
import type { SearchSession, SourcesResponse } from '../src/types/api'

const operational: SourcesResponse = { ...catalog, searchAvailable: true, sources: [
  { id: 'google-books', name: 'Google Books', integrationType: 'api', implementationStatus: 'available',
    capabilities: [{ id: 'general', status: 'confirmed' }], notes: 'Bounded metadata search.' }, ...catalog.sources,
] }
function setupApi() {
  const session: SearchSession = { id: 'a'.repeat(32), state: 'completed', createdAt: '2026-10-28T10:00:00Z',
    finished: true, cancellationRequested: false, hasErrors: false, sources: [{ sourceId: 'google-books',
      state: 'empty', recordsRetrieved: 0, recordsAccepted: 0, recordsRejected: 0, searchCompleted: true,
      localFiltersApplied: true, coverageIncomplete: true, resultLimitReached: false, filters: [], error: null }] }
  const fetch = vi.fn(async (path: string) => jsonResponse(path === '/api/health' ? health
    : path === '/api/sources' ? operational : path === '/api/search' ? { apiVersion: '1', session }
    : { apiVersion: '1', session, results: [] }))
  vi.stubGlobal('fetch', fetch)
  return fetch
}

describe('compact library workspace', () => {
  it('shares query and mode across quick search, passage search and the main form', async () => {
    const fetch = setupApi(); const app = mount(App); await flushPromises()
    await app.find('#quick-query').setValue('A shared query')
    expect(app.find<HTMLInputElement>('#search-query').element.value).toBe('A shared query')
    await app.find('nav[aria-label="Navegação lateral"] a[href="#passage"]').trigger('click')
    expect(app.find('textarea#search-query').exists()).toBe(true)
    expect(app.find<HTMLInputElement>('input[value="passage"]').element.checked).toBe(true)
    await app.find('#search-query').setValue('An exact phrase')
    expect(app.find<HTMLInputElement>('#quick-query').element.value).toBe('An exact phrase')
    await app.find('.quick-search').trigger('submit'); await flushPromises()
    const request = fetch.mock.calls.find(([path]) => path === '/api/search')
    expect(request).toBeDefined()
    expect(fetch).toHaveBeenCalledWith('/api/search', expect.objectContaining({ body: expect.stringContaining('"field":"passage"') }))
    expect(fetch).toHaveBeenCalledWith('/api/search', expect.objectContaining({ body: expect.stringContaining('An exact phrase') }))
    expect(app.find('.top-navigation a[href="#search"]').attributes('aria-current')).toBe('location')
    app.unmount()
  })

  it('opens real filter sections from the sidebar and applies the historical shortcut', async () => {
    setupApi(); const app = mount(App); await flushPromises()
    await app.find('nav[aria-label="Navegação lateral"] a[href="#advanced"]').trigger('click')
    expect(app.find<HTMLDetailsElement>('#advanced').element.open).toBe(true)
    expect(app.find('.search-toolbar button').attributes('aria-expanded')).toBe('true')
    await app.find('.period-guide button').trigger('click')
    expect(app.find<HTMLInputElement>('#year-from').element.value).toBe('1801')
    expect(app.find<HTMLInputElement>('#year-to').element.value).toBe('1900')
    await app.find('nav[aria-label="Navegação principal"] a[href="#sources"]').trigger('click')
    expect(app.find('.top-navigation a[href="#sources"]').attributes('aria-current')).toBe('location')
    expect(app.find('#sources h2').text()).toBe('Estado das bibliotecas')
    app.unmount()
  })

  it('keeps filters and values when a desktop session moves them to the sidebar and back on resize', async () => {
    setupApi()
    let change!: () => void
    const media = { matches: true, addEventListener: vi.fn((_name, callback) => { change = callback }), removeEventListener: vi.fn() }
    vi.stubGlobal('matchMedia', vi.fn(() => media))
    const app = mount(App); await flushPromises()
    await app.find('#search-query').setValue('History')
    await app.find('#period').setValue('nineteenth')
    await app.find('#language').setValue('la')
    await app.find('#bibliographic-search').trigger('submit'); await flushPromises()
    expect(app.find('.workspace-sidebar #advanced').exists()).toBe(true)
    expect(app.findAll('#advanced')).toHaveLength(1)
    expect(app.find<HTMLInputElement>('#year-from').element.value).toBe('1801')
    expect(app.find('.source-progress').text()).toContain('Google Books')
    media.matches = false; change(); await flushPromises()
    expect(app.find('.workspace-center #advanced').exists()).toBe(true)
    expect(app.findAll('#language')).toHaveLength(1)
    expect(app.find<HTMLSelectElement>('#language').element.value).toBe('la')
    app.unmount(); expect(media.removeEventListener).toHaveBeenCalledWith('change', change)
  })

  it('provides an expandable mobile navigation without removing any links', async () => {
    setupApi(); const app = mount(App); await flushPromises()
    const toggle = app.find('.sidebar-toggle')
    expect(toggle.attributes('aria-expanded')).toBe('false')
    await toggle.trigger('click')
    expect(toggle.attributes('aria-expanded')).toBe('true')
    expect(app.find('#sidebar-menu').classes()).toContain('is-open')
    expect(app.findAll('nav[aria-label="Navegação lateral"] a')).toHaveLength(5)
    await app.find('nav[aria-label="Navegação lateral"] a[href="#search"]').trigger('click')
    expect(toggle.attributes('aria-expanded')).toBe('false')
    app.unmount()
  })

  it('shows API status without offering decorative theme or language controls', async () => {
    setupApi(); const app = mount(App); await flushPromises()
    expect(app.find('.site-header').text()).toContain('API conectada')
    expect(app.find('.site-header').text()).toContain('Google Books')
    expect(app.find('.sidebar-group').text()).toContain('Google Books')
    expect(app.find('.site-header select').exists()).toBe(false)
    expect(app.find('.hero').exists()).toBe(false)
    app.unmount()
  })
})
