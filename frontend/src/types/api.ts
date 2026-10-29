export interface HealthResponse {
  status: 'ok'
  application: 'LiberOmnia'
  apiVersion: '1'
}

export interface SourceCapability {
  id: string
  status: 'planned' | 'confirmed'
}

export const operationalStates = ['disabled', 'configuration_required', 'unverified', 'available', 'rate_limited',
  'quota_exhausted', 'configuration_error', 'permission_denied', 'communication_error', 'temporary_failure'] as const
export interface SourceOperationalStatus {
  implemented: boolean
  configured: boolean
  searchAllowed: boolean
  state: typeof operationalStates[number]
  lastSuccessfulAt: number | null
  error: ApiErrorBody | null
}

export interface Source {
  id: string
  name: string
  integrationType: 'api' | 'external'
  implementationStatus: 'planned' | 'available' | 'temporarily_unavailable'
  operational?: SourceOperationalStatus
  filterSupport?: { publication: FilterSupport; language: FilterSupport; availability: FilterSupport }
  capabilities: SourceCapability[]
  notes: string
}

export interface SourcesResponse {
  apiVersion: '1'
  searchAvailable: boolean
  sources: Source[]
}

export type LoadState<T> =
  | { status: 'loading' }
  | { status: 'success'; data: T }
  | { status: 'error'; message: string }

export type SearchField = 'general' | 'title' | 'author' | 'subject' | 'passage'
export type AccessAvailability = 'viewable' | 'pdf' | 'epub' | 'restricted' | 'metadata_only' | 'unknown'
export type FilterSupport = 'native' | 'local' | 'unsupported'
export type SourceState = 'pending' | 'running' | 'completed' | 'empty' | 'rate_limited' | 'blocked'
  | 'failed' | 'timed_out' | 'cancelled' | 'unsupported' | 'disabled' | 'external_only'
export type SessionState = 'pending' | 'running' | 'completed' | 'partial' | 'failed' | 'cancelled'

export interface SearchFilters {
  publication?: { from?: number | null; to?: number | null }
  language?: string | null
  sourceIds?: string[]
  availability?: AccessAvailability | null
  maxResultsPerSource?: number
}
export interface SearchRequest extends SearchFilters {
  text: string
  field?: SearchField
}
export interface ApiErrorBody { code: string; message: string }
export interface SourceProgress {
  sourceId: string
  state: SourceState
  recordsRetrieved: number
  recordsAccepted: number
  recordsRejected: number
  searchCompleted: boolean
  localFiltersApplied: boolean
  fallbackUsed?: boolean
  coverageIncomplete: boolean
  resultLimitReached: boolean
  filters: { id: string; support: FilterSupport }[]
  error: ApiErrorBody | null
}
export interface SearchSession {
  id: string
  state: SessionState
  createdAt: string
  finished: boolean
  cancellationRequested: boolean
  hasErrors: boolean
  sources: SourceProgress[]
}
export interface AccessInfo { availability: AccessAvailability; url: string | null; viewability?: string | null; format?: string | null }
export interface TextualEvidence {
  kind: 'confirmed_occurrence' | 'source_excerpt' | 'approximate_match' | 'unverified'
  text: string | null
  location: string | null
}
export interface BibliographicRecord {
  sourceId: string
  sourceRecordId: string
  title: string
  subtitle?: string | null
  publishedDate?: string | null
  accessCountry?: string | null
  publicDomain?: boolean | null
  pdfAvailable?: boolean | null
  epubAvailable?: boolean | null
  previewUrl?: string | null
  infoUrl?: string | null
  authors: string[]
  edition: string | null
  publisher: string | null
  publicationYear: number | null
  originalPublicationYear: number | null
  language: string | null
  volume: string | null
  workId: string | null
  editionId: string | null
  identifiers: { scheme: string; value: string }[]
  originalUrl: string | null
  access: AccessInfo[]
  textualEvidence: TextualEvidence[]
}
export interface SearchSessionResponse { apiVersion: '1'; session: SearchSession }
export interface SearchResultsResponse extends SearchSessionResponse {
  results: { record: BibliographicRecord; original: BibliographicRecord }[]
}
