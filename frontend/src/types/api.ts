export interface HealthResponse {
  status: 'ok'
  application: 'LiberOmnia'
  apiVersion: '1'
}

export interface SourceCapability {
  id: string
  status: 'planned' | 'confirmed'
}

export interface Source {
  id: string
  name: string
  integrationType: 'api' | 'external'
  implementationStatus: 'planned' | 'available'
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
