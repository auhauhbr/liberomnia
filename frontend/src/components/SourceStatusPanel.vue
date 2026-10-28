<script setup lang="ts">
import type { LoadState, SearchSession, SourcesResponse, SourceState } from '../types/api'
const props = defineProps<{ session: SearchSession; catalog: LoadState<SourcesResponse> }>()
const labels: Record<SourceState, string> = {
  pending: 'Na fila', running: 'Pesquisando', completed: 'Concluída', empty: 'Sem resultados', rate_limited: 'Limite de requisições',
  blocked: 'Acesso bloqueado', failed: 'Falha na fonte', timed_out: 'Tempo esgotado', cancelled: 'Cancelada',
  unsupported: 'Não suportada', disabled: 'Indisponível', external_only: 'Integração externa',
}
function sourceName(id: string) { return props.catalog.status === 'success' ? props.catalog.data.sources.find(source => source.id === id)?.name ?? id : id }
</script>

<template>
  <section class="panel source-progress" aria-labelledby="progress-title">
    <h2 id="progress-title">Progresso da pesquisa</h2>
    <ul aria-live="polite">
      <li v-for="source in session.sources" :key="source.sourceId">
        <div class="source-progress-heading"><strong>{{ sourceName(source.sourceId) }}</strong><span>{{ labels[source.state] }}</span></div>
        <p>{{ source.recordsAccepted }} aceito(s) · {{ source.recordsRetrieved }} recuperado(s)</p>
        <p v-if="source.error" class="field-error">{{ source.error.message }}</p>
      </li>
    </ul>
  </section>
</template>
