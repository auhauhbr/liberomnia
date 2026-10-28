import { defineComponent } from 'vue'
import { flushPromises, mount } from '@vue/test-utils'
import { afterEach, describe, expect, it, vi } from 'vitest'
import App from '../src/App.vue'
import BibliographicCard from '../src/components/BibliographicCard.vue'
import SearchPanel from '../src/components/SearchPanel.vue'
import { useSearchSession } from '../src/composables/useSearchSession'
import type { BibliographicRecord, SearchResultsResponse, SearchSession, SourcesResponse } from '../src/types/api'
import { catalog, health, jsonResponse } from './fixtures'

const id = 'a'.repeat(32)
const available: SourcesResponse = { ...catalog, searchAvailable: true, sources: [
  { id: 'google-books', name: 'Google Books', integrationType: 'api', implementationStatus: 'available',
    capabilities: [{ id: 'general', status: 'confirmed' }], notes: 'Bounded Google Books search.' }, ...catalog.sources,
] }
const session: SearchSession = { id, state: 'running', createdAt: '2026-10-28T10:00:00Z', finished: false,
  cancellationRequested: false, hasErrors: false, sources: [{ sourceId: 'google-books', state: 'running',
    recordsRetrieved: 0, recordsAccepted: 0, recordsRejected: 0, searchCompleted: false, localFiltersApplied: true,
    coverageIncomplete: true, resultLimitReached: false, filters: [{ id: 'publication', support: 'local' }], error: null }],
}
const record: BibliographicRecord = { sourceId: 'google-books', sourceRecordId: 'api-volume-id',
  title: 'Title returned by the source', subtitle: 'Source subtitle', authors: ['Source author'], publisher: 'Source publisher',
  publicationYear: 1880, publishedDate: '1880-05', originalPublicationYear: null, language: 'la', edition: null,
  volume: null, workId: null, editionId: null, identifiers: [{ scheme: 'OTHER', value: 'source-identifier' }],
  originalUrl: 'https://books.google.com/books?id=api-volume-id',
  access: [{ availability: 'viewable', url: 'https://books.google.com/preview', viewability: 'PARTIAL' }],
  textualEvidence: [{ kind: 'source_excerpt', text: '<img src=x onerror=alert(1)> source excerpt', location: null }],
}
function results(state = session, records = [record]): SearchResultsResponse {
  return { apiVersion: '1', session: state, results: records.map(record => ({ record, original: record })) }
}
const complete = { ...session, finished: true, state: 'completed' as const,
  sources: [{ ...session.sources[0]!, state: 'completed' as const, recordsAccepted: 1, fallbackUsed: true }] }
const cancelled = { ...session, finished: true, state: 'cancelled' as const, cancellationRequested: true }
function mockApi() {
  const fetch = vi.fn(async (path: string) => {
    if (path === '/api/health') return jsonResponse(health)
    if (path === '/api/sources') return jsonResponse(available)
    if (path === '/api/search') return jsonResponse({ apiVersion: '1', session }, 202)
    if (path.endsWith('/cancel')) return jsonResponse({ apiVersion: '1', session: cancelled })
    return jsonResponse(results(complete))
  })
  vi.stubGlobal('fetch', fetch)
  return fetch
}
afterEach(() => { vi.useRealTimers() })

describe('bibliographic search flow', () => {
  it('submits selected source, modes and historical filters and displays only API results safely', async () => {
    const fetch = mockApi()
    const app = mount(App)
    await flushPromises()
    expect(app.findAll('.bibliographic-card')).toHaveLength(0)
    expect(app.find<HTMLInputElement>('input[value="google-books"]').element.checked).toBe(true)
    expect(app.find<HTMLInputElement>('input[value="gallica"]').element.disabled).toBe(true)
    await app.find('#search-query').setValue('Aquinas')
    await app.find('input[value="subject"]').setValue()
    await app.find('#period').setValue('nineteenth')
    await app.find('#language').setValue('la')
    await app.find('#availability').setValue('viewable')
    await app.find('form').trigger('submit')
    await flushPromises()
    expect(fetch).toHaveBeenCalledWith('/api/search', expect.objectContaining({ body: JSON.stringify({ text: 'Aquinas',
      field: 'subject', sourceIds: ['google-books'], language: 'la', availability: 'viewable',
      publication: { from: 1801, to: 1900 }, maxResultsPerSource: 40 }) }))
    expect(app.text()).toContain(record.title)
    expect(app.text()).toContain('1880')
    expect(app.text()).toContain('Visualização parcial')
    expect(app.text()).toContain('fallback')
    expect(app.text()).toContain('Cobertura incompleta')
    expect(app.find('.bibliographic-card--local').exists()).toBe(true)
    expect(app.find('.source-excerpt').text()).toContain('<img src=x onerror=alert(1)>')
    expect(app.find('.source-excerpt img').exists()).toBe(false)
    expect(app.find('.bibliographic-card a').attributes('rel')).toBe('noopener noreferrer')
    app.unmount()
  })
  it('cancels a running session and stops polling automatically', async () => {
    vi.useFakeTimers()
    const fetch = mockApi()
    let stopped = false
    fetch.mockImplementation(async (path: string) => {
      if (path === '/api/health') return jsonResponse(health)
      if (path === '/api/sources') return jsonResponse(available)
      if (path === '/api/search') return jsonResponse({ apiVersion: '1', session }, 202)
      if (path.endsWith('/cancel')) { stopped = true; return jsonResponse({ apiVersion: '1', session: cancelled }) }
      return jsonResponse(results(stopped ? cancelled : session, []))
    })
    const app = mount(App); await flushPromises()
    await app.find('#search-query').setValue('Aquinas'); await app.find('form').trigger('submit'); await flushPromises()
    expect(app.text()).toContain('Pesquisando nos acervos')
    await app.find('.cancel-search').trigger('click'); await flushPromises()
    expect(app.text()).toContain('Pesquisa cancelada')
    const count = fetch.mock.calls.length
    await vi.advanceTimersByTimeAsync(10000)
    expect(fetch.mock.calls.length).toBe(count)
    app.unmount()
  })
  it('updates the catalog and prevents selecting sources that became unavailable', async () => {
    const panel = mount(SearchPanel, { props: { catalog: { status: 'success', data: available } } })
    await panel.find('#search-query').setValue('Title')
    expect(panel.find<HTMLButtonElement>('button[type="submit"]').element.disabled).toBe(false)
    await panel.find('input[value="google-books"]').setValue(false)
    expect(panel.find<HTMLButtonElement>('button[type="submit"]').element.disabled).toBe(true)
    await panel.setProps({ catalog: { status: 'success', data: catalog } })
    await panel.find('form').trigger('submit')
    expect(panel.emitted('search')).toBeUndefined()
    const fetch = mockApi(); const app = mount(App); await flushPromises()
    await app.find('.section-heading button').trigger('click'); await flushPromises()
    expect(fetch.mock.calls.filter(([path]) => path === '/api/sources')).toHaveLength(2)
    app.unmount(); panel.unmount()
  })
  it('rejects unsafe links and never turns restricted tokens into downloads', () => {
    const card = mount(BibliographicCard, { props: { record: { ...record, originalUrl: 'javascript:alert(1)',
      infoUrl: 'https://user:password@example.com', access: [{ availability: 'pdf', url: 'data:text/html,hi' },
        { availability: 'restricted', url: 'https://books.google.com/token', format: 'epub' }] } } })
    expect(card.findAll('a')).toHaveLength(0)
    expect(card.text()).toContain('Acesso restrito (EPUB)')
    card.unmount()
  })
})
function harness(options = { interval: 750, maxPolls: 60, maxDuration: 60000 }) {
  let search!: ReturnType<typeof useSearchSession>
  const wrapper = mount(defineComponent({ setup() { search = useSearchSession(options); return () => null } }))
  return { wrapper, get search() { return search } }
}
describe('session lifecycle', () => {
  it('replaces snapshots without duplicates and stops when finished', async () => {
    vi.useFakeTimers(); const fetch = mockApi(); let poll = 0
    fetch.mockImplementation(async path => path === '/api/search' ? jsonResponse({ apiVersion: '1', session })
      : jsonResponse(results(++poll === 1 ? session : complete)))
    const h = harness(); await h.search.start({ text: 'one' })
    expect(h.search.records.value).toHaveLength(1)
    await vi.advanceTimersByTimeAsync(750); expect(h.search.records.value).toHaveLength(1)
    const count = fetch.mock.calls.length; await vi.advanceTimersByTimeAsync(20000); expect(fetch.mock.calls.length).toBe(count)
    h.wrapper.unmount()
  })
  it('ignores late results from an older search and aborts pending requests on unmount', async () => {
    let old!: (response: Response) => void
    let count = 0; const signals: AbortSignal[] = []
    vi.stubGlobal('fetch', vi.fn(async (path: string, options: RequestInit) => {
      signals.push(options.signal as AbortSignal)
      if (path === '/api/search') return jsonResponse({ apiVersion: '1', session })
      if (++count === 1) return new Promise<Response>(resolve => { old = resolve })
      return jsonResponse(results(complete, [{ ...record, title: 'New search' }]))
    }))
    const h = harness(); const first = h.search.start({ text: 'old' }); await flushPromises()
    await h.search.start({ text: 'new' }); old(jsonResponse(results(complete, [{ ...record, title: 'Old search' }]))); await first
    expect(h.search.records.value[0]?.title).toBe('New search')
    expect(signals[1]?.aborted).toBe(true)
    h.wrapper.unmount()
  })
  it('aborts an active poll on unmount', async () => {
    let signal!: AbortSignal
    vi.stubGlobal('fetch', vi.fn(async (path: string, options: RequestInit) => {
      if (path === '/api/search') return jsonResponse({ apiVersion: '1', session })
      signal = options.signal as AbortSignal
      return new Promise<Response>((_resolve, reject) => signal.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError'))))
    }))
    const h = harness(); const pending = h.search.start({ text: 'title' }); await flushPromises(); h.wrapper.unmount()
    expect(signal.aborted).toBe(true); await pending
  })
  it('bounds polling and preserves results after disconnection', async () => {
    vi.useFakeTimers(); const fetch = mockApi()
    fetch.mockImplementation(async path => path === '/api/search' ? jsonResponse({ apiVersion: '1', session }) : jsonResponse(results()))
    const h = harness({ interval: 10, maxPolls: 2, maxDuration: 1000 }); await h.search.start({ text: 'title' })
    await vi.advanceTimersByTimeAsync(30)
    expect(h.search.error.value).toContain('Limite de acompanhamento')
    expect(fetch.mock.calls.filter(([path]) => path.endsWith('/results'))).toHaveLength(2)
    h.wrapper.unmount()
    const other = harness(); await other.search.start({ text: 'title' })
    fetch.mockRejectedValue(new TypeError('Failed to fetch'))
    await vi.advanceTimersByTimeAsync(750)
    expect(other.search.error.value).toContain('Conexão interrompida')
    expect(other.search.records.value).toHaveLength(1)
    other.wrapper.unmount()
  })
  it('cancels even if creation has not yet returned its session ID', async () => {
    let resolve!: (response: Response) => void; const fetch = mockApi()
    fetch.mockImplementation(async path => {
      if (path === '/api/search') return new Promise<Response>(r => { resolve = r })
      if (path.endsWith('/cancel')) return jsonResponse({ apiVersion: '1', session: cancelled })
      return jsonResponse(results(cancelled, []))
    })
    const h = harness(); const pending = h.search.start({ text: 'title' }); await h.search.cancel()
    resolve(jsonResponse({ apiVersion: '1', session })); await pending
    expect(fetch.mock.calls.some(([path]) => path.endsWith('/cancel'))).toBe(true)
    expect(h.search.session.value?.state).toBe('cancelled'); h.wrapper.unmount()
  })
})
