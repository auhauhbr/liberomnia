<script setup lang="ts">
import type { BibliographicRecord, SearchSession, SourceState } from '../types/api'
import BibliographicCard from './BibliographicCard.vue'

defineProps<{ session: SearchSession | null; records: BibliographicRecord[]; busy: boolean; error: string }>()
defineEmits<{ resume: [] }>()
const states: Record<SourceState, string> = {
  pending: 'Aguardando pesquisa', running: 'Pesquisa em andamento', completed: 'Pesquisa concluída', empty: 'Nenhum resultado',
  rate_limited: 'Limite de requisições atingido', blocked: 'A fonte restringiu o acesso', failed: 'Erro da biblioteca',
  timed_out: 'Tempo limite da fonte excedido', cancelled: 'Pesquisa cancelada', unsupported: 'Pesquisa não suportada',
  disabled: 'Fonte indisponível', external_only: 'Fonte externa',
}
</script>

<template>
  <section v-if="busy || session || error" class="panel search-results" aria-label="Resultados bibliográficos" :aria-busy="busy">
    <div role="status" aria-live="polite">
      <h2>{{ busy ? 'Pesquisando nos acervos…' : session?.state === 'cancelled' ? 'Pesquisa cancelada' : 'Resultados da pesquisa' }}</h2>
      <p v-if="session?.state === 'partial'">Resultados parciais. Uma parte da pesquisa não pôde ser concluída.</p>
      <p class="result-source-status" v-for="source in session?.sources" :key="source.sourceId">
        {{ source.sourceId === 'google-books' ? 'Google Books' : source.sourceId }}: {{ states[source.state] }}.
        <span v-if="source.fallbackUsed">Foi usada uma pesquisa geral alternativa (fallback), com verificação local do campo. Os resultados podem ser incompletos.</span>
        <span v-if="source.localFiltersApplied">Filtros aplicados localmente aos registros recuperados.</span>
        <span v-if="source.coverageIncomplete">Cobertura incompleta: esta pesquisa não esgota o acervo.</span>
        <span v-if="source.resultLimitReached">Limite de páginas, chamadas ou resultados atingido.</span>
        <span v-if="source.error">{{ source.error.message }}</span>
      </p>
      <p v-if="session?.finished && !records.length && !session.hasErrors && session.state !== 'cancelled'">Nenhum resultado nos registros consultados para estes critérios.</p>
    </div>
    <div v-if="error" role="alert" class="state-box state-box--error"><p>{{ error }}</p>
      <button v-if="session && !session.finished" type="button" class="text-button" @click="$emit('resume')">Retomar acompanhamento</button>
    </div>
    <p v-if="records.length">{{ records.length }} resultado(s) · Dados bibliográficos fornecidos pelas fontes.</p>
    <BibliographicCard v-for="record in records" :key="JSON.stringify([record.sourceId, record.sourceRecordId])" :record="record"
      :local="session?.sources.find(source => source.sourceId === record.sourceId)?.localFiltersApplied"
      :fallback="session?.sources.find(source => source.sourceId === record.sourceId)?.fallbackUsed" />
  </section>
</template>
