import { isNonemptyString, isObject, requestJson } from './api'
import type {
  BibliographicRecord, SearchRequest, SearchResultsResponse, SearchSession,
  SearchSessionResponse, SourceProgress,
} from '../types/api'

const sourceStates = new Set(['pending', 'running', 'completed', 'empty', 'rate_limited', 'blocked',
  'failed', 'timed_out', 'cancelled', 'unsupported', 'disabled', 'external_only'])
const sessionStates = new Set(['pending', 'running', 'completed', 'partial', 'failed', 'cancelled'])
const availabilityStates = new Set(['viewable', 'pdf', 'epub', 'restricted', 'metadata_only', 'unknown'])
const evidenceKinds = new Set(['confirmed_occurrence', 'source_excerpt', 'approximate_match', 'unverified'])
const filterSupports = new Set(['native', 'local', 'unsupported'])
const nullableString = (value: unknown) => value === null || typeof value === 'string'
const count = (value: unknown) => typeof value === 'number' && Number.isSafeInteger(value) && value >= 0
const year = (value: unknown) => value === null || (count(value) && Number(value) >= 1 && Number(value) <= 9999)
const member = (value: unknown, allowed: Set<string>) => typeof value === 'string' && allowed.has(value)
const identifier = (value: unknown) => typeof value === 'string' && /^[a-f0-9]{32}$/.test(value)

function safeUrl(value: unknown): boolean {
  if (value === null) return true
  if (typeof value !== 'string') return false
  try {
    const url = new URL(value)
    return (url.protocol === 'https:' || url.protocol === 'http:') && !url.username && !url.password
  } catch { return false }
}

function isProgress(value: unknown): value is SourceProgress {
  return isObject(value) && isNonemptyString(value.sourceId) && member(value.state, sourceStates)
    && count(value.recordsRetrieved) && count(value.recordsAccepted) && count(value.recordsRejected)
    && ['searchCompleted', 'localFiltersApplied', 'coverageIncomplete', 'resultLimitReached']
      .every(key => typeof value[key] === 'boolean')
    && (value.error === null || (isObject(value.error) && isNonemptyString(value.error.code) && isNonemptyString(value.error.message)))
    && Array.isArray(value.filters) && value.filters.every(filter => isObject(filter)
      && isNonemptyString(filter.id) && member(filter.support, filterSupports))
}

function isSession(value: unknown): value is SearchSession {
  return isObject(value) && identifier(value.id) && member(value.state, sessionStates)
    && typeof value.createdAt === 'string' && /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/.test(value.createdAt)
    && Number.isFinite(Date.parse(value.createdAt))
    && ['finished', 'cancellationRequested', 'hasErrors'].every(key => typeof value[key] === 'boolean')
    && Array.isArray(value.sources) && value.sources.length <= 16 && value.sources.every(isProgress)
    && new Set(value.sources.map(source => source.sourceId)).size === value.sources.length
}

function isRecord(value: unknown): value is BibliographicRecord {
  return isObject(value) && isNonemptyString(value.sourceId) && isNonemptyString(value.sourceRecordId)
    && isNonemptyString(value.title) && Array.isArray(value.authors) && value.authors.every(author => typeof author === 'string')
    && ['edition', 'publisher', 'language', 'volume', 'workId', 'editionId'].every(key => nullableString(value[key]))
    && year(value.publicationYear) && year(value.originalPublicationYear) && safeUrl(value.originalUrl)
    && Array.isArray(value.identifiers) && value.identifiers.every(id => isObject(id)
      && isNonemptyString(id.scheme) && isNonemptyString(id.value))
    && Array.isArray(value.access) && value.access.every(access => isObject(access)
      && member(access.availability, availabilityStates) && safeUrl(access.url))
    && Array.isArray(value.textualEvidence) && value.textualEvidence.every(evidence => isObject(evidence)
      && member(evidence.kind, evidenceKinds) && nullableString(evidence.text) && nullableString(evidence.location))
}

function sessionResponse(value: unknown): SearchSessionResponse {
  if (!isObject(value) || value.apiVersion !== '1' || !isSession(value.session)) {
    throw new Error('A API retornou uma sessão de pesquisa incompatível.')
  }
  return { apiVersion: '1', session: value.session }
}

function sessionPath(id: string): string {
  if (!identifier(id)) throw new Error('Identificador de sessão inválido.')
  return `/api/search/${encodeURIComponent(id)}`
}

// Prepared for real adapters; the main interface intentionally does not call these services.
export async function createSearchSession(request: SearchRequest, signal: AbortSignal): Promise<SearchSessionResponse> {
  return sessionResponse(await requestJson('/api/search', signal, 'POST', request))
}

export async function getSearchSession(id: string, signal: AbortSignal): Promise<SearchSessionResponse> {
  return sessionResponse(await requestJson(sessionPath(id), signal))
}

export async function cancelSearchSession(id: string, signal: AbortSignal): Promise<SearchSessionResponse> {
  return sessionResponse(await requestJson(`${sessionPath(id)}/cancel`, signal, 'POST'))
}

export async function getSearchResults(id: string, signal: AbortSignal): Promise<SearchResultsResponse> {
  const value = await requestJson(`${sessionPath(id)}/results`, signal)
  const session = sessionResponse(value)
  if (!isObject(value) || !Array.isArray(value.results) || value.results.length > 1000) {
    throw new Error('A API retornou resultados de pesquisa incompatíveis.')
  }
  const keys = new Set<string>()
  const results: SearchResultsResponse['results'] = []
  for (const item of value.results) {
    if (!isObject(item) || !isRecord(item.record) || !isRecord(item.original)
      || item.record.sourceId !== item.original.sourceId || item.record.sourceRecordId !== item.original.sourceRecordId
      || !session.session.sources.map(source => source.sourceId).includes(item.record.sourceId)) {
      throw new Error('A API retornou resultados de pesquisa incompatíveis.')
    }
    const key = JSON.stringify([item.record.sourceId, item.record.sourceRecordId])
    if (keys.has(key)) throw new Error('A API retornou registros duplicados.')
    keys.add(key)
    results.push({ record: item.record, original: item.original })
  }
  return { ...session, results }
}
