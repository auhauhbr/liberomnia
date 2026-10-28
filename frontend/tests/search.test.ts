import { describe, expect, it, vi } from 'vitest'
import { ApiError, getSources } from '../src/services/api'
import { cancelSearchSession, createSearchSession, getSearchResults, getSearchSession } from '../src/services/search'
import type { BibliographicRecord, SearchSessionResponse } from '../src/types/api'
import { catalog, jsonResponse } from './fixtures'

const id = 'a'.repeat(32)
const session: SearchSessionResponse = {
  apiVersion: '1', session: {
    id, state: 'running', createdAt: '2026-10-28T10:00:00Z', finished: false,
    cancellationRequested: false, hasErrors: false,
    sources: [{ sourceId: 'test-source', state: 'running', recordsRetrieved: 0,
      recordsAccepted: 0, recordsRejected: 0, searchCompleted: false, localFiltersApplied: true,
      coverageIncomplete: true, resultLimitReached: false,
      filters: [{ id: 'publication', support: 'local' }], error: null }],
  },
}
const record: BibliographicRecord = {
  sourceId: 'test-source', sourceRecordId: 'edition-1', title: 'Test record', authors: [],
  edition: null, publisher: null, publicationYear: 1899, originalPublicationYear: 1840,
  language: null, volume: null, workId: 'work-1', editionId: 'edition-1', identifiers: [],
  originalUrl: null, access: [{ availability: 'metadata_only', url: null }],
  textualEvidence: [{ kind: 'unverified', text: null, location: null }],
}
const signal = () => new AbortController().signal

function mock(body: unknown, status = 200) {
  const fetch = vi.fn().mockResolvedValue(jsonResponse(body, status))
  vi.stubGlobal('fetch', fetch)
  return fetch
}

describe('search session services', () => {
  it('posts a typed request and accepts a 202 session', async () => {
    const fetch = mock({ ...session, session: { ...session.session, state: 'pending' } }, 202)
    const request = { text: 'research', field: 'subject' as const,
      publication: { from: 1801, to: 1900 }, sourceIds: ['test-source'], maxResultsPerSource: 20 }
    const result = await createSearchSession(request, signal())
    expect(result.session.state).toBe('pending')
    expect(fetch).toHaveBeenCalledWith('/api/search', expect.objectContaining({
      method: 'POST', body: JSON.stringify(request),
      headers: { Accept: 'application/json', 'Content-Type': 'application/json' },
    }))
  })

  it('retrieves progress and sends cancellation to the correct routes', async () => {
    const fetch = mock(session)
    expect(await getSearchSession(id, signal())).toEqual(session)
    fetch.mockResolvedValueOnce(jsonResponse({ ...session, session: {
      ...session.session, state: 'cancelled', finished: true, cancellationRequested: true,
    } }))
    expect((await cancelSearchSession(id, signal())).session.cancellationRequested).toBe(true)
    expect(fetch).toHaveBeenNthCalledWith(1, `/api/search/${id}`, expect.objectContaining({ method: 'GET' }))
    expect(fetch).toHaveBeenNthCalledWith(2, `/api/search/${id}/cancel`, expect.objectContaining({ method: 'POST' }))
  })

  it('preserves original records, separate dates, unknown metadata, access and evidence', async () => {
    const results = { ...session, results: [{ record, original: { ...record, title: ' Test  record ' } }] }
    mock(results)
    const response = await getSearchResults(id, signal())
    expect(response).toEqual(results)
    expect(response.results[0]?.record.publicationYear).toBe(1899)
    expect(response.results[0]?.record.originalPublicationYear).toBe(1840)
    expect(response.results[0]?.record.originalUrl).toBeNull()
    expect(response.results[0]?.record.access[0]?.availability).toBe('metadata_only')
    expect(response.results[0]?.record.textualEvidence[0]?.kind).toBe('unverified')
  })

  it.each([
    [503, 'no_available_providers'], [422, 'unsupported_search'], [422, 'unknown_source'],
    [404, 'session_not_found'], [410, 'session_expired'], [429, 'session_limit_reached'],
  ])('preserves structured HTTP %i errors (%s)', async (status, code) => {
    mock({ apiVersion: '1', error: { code, message: 'Safe server message' } }, status)
    const result = getSearchSession(id, signal())
    await expect(result).rejects.toBeInstanceOf(ApiError)
    await expect(result).rejects.toMatchObject({ status, code, message: 'Safe server message' })
  })

  it.each([
    {}, { ...session, apiVersion: '2' }, { ...session, session: { ...session.session, id: '../health' } },
    { ...session, session: { ...session.session, state: 'success' } },
    { ...session, session: { ...session.session, finished: 'false' } },
    { ...session, session: { ...session.session, createdAt: 'not-a-date' } },
    { ...session, session: { ...session.session, sources: [{ ...session.session.sources[0], state: 'unknown' }] } },
    { ...session, session: { ...session.session, sources: [{ ...session.session.sources[0], recordsRetrieved: -1 }] } },
  ])('rejects incompatible session contracts %#', async (body) => {
    mock(body)
    await expect(getSearchSession(id, signal())).rejects.toThrow('sessão de pesquisa incompatível')
  })

  it.each([
    { ...session },
    { ...session, results: [{ record: { ...record, publicationYear: 0 }, original: record }] },
    { ...session, results: [{ record: { ...record, originalUrl: 'javascript:alert(1)' }, original: record }] },
    { ...session, results: [{ record, original: { ...record, sourceId: 'other' } }] },
    { ...session, results: [{ record: { ...record, access: [{ availability: 'free', url: null }] }, original: record }] },
    { ...session, results: [{ record: { ...record, textualEvidence: [{ kind: 'proven', text: null, location: null }] }, original: record }] },
  ])('rejects invalid records and provenance %#', async (body) => {
    mock(body)
    await expect(getSearchResults(id, signal())).rejects.toThrow('resultados de pesquisa incompatíveis')
  })

  it('rejects duplicate source-record identities', async () => {
    mock({ ...session, results: [{ record, original: record }, { record, original: record }] })
    await expect(getSearchResults(id, signal())).rejects.toThrow('registros duplicados')
  })

  it('rejects arbitrary paths without making a request', async () => {
    const fetch = mock(session)
    await expect(getSearchSession('../health', signal())).rejects.toThrow('Identificador')
    expect(fetch).not.toHaveBeenCalled()
  })

  it('propagates caller cancellation', async () => {
    const controller = new AbortController()
    vi.stubGlobal('fetch', vi.fn((_path: string, options: RequestInit) => new Promise((_resolve, reject) => {
      options.signal?.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')))
    })))
    const result = getSearchSession(id, controller.signal)
    controller.abort()
    await expect(result).rejects.toMatchObject({ name: 'AbortError' })
  })

  it('accepts temporarily unavailable sources and explicit filter support', async () => {
    const data = { ...catalog, sources: [{ ...catalog.sources[0], implementationStatus: 'temporarily_unavailable',
      filterSupport: { publication: 'local', language: 'native', availability: 'unsupported' } }] }
    mock(data)
    expect(await getSources(signal())).toEqual(data)
  })
})
