import type { HealthResponse, SourcesResponse } from '../src/types/api'

export const health: HealthResponse = { status: 'ok', application: 'LiberOmnia', apiVersion: '1' }
export const catalog: SourcesResponse = {
  apiVersion: '1', searchAvailable: false,
  sources: [
    { id: 'gallica', name: 'Gallica', integrationType: 'api', implementationStatus: 'planned',
      capabilities: [{ id: 'title', status: 'planned' }, { id: 'passage', status: 'planned' }],
      notes: 'Coverage must be validated.' },
    { id: 'annas-archive', name: "Anna's Archive", integrationType: 'external', implementationStatus: 'planned',
      capabilities: [{ id: 'externalSearch', status: 'planned' }], notes: 'External search only.' },
  ],
}

export function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json' } })
}
