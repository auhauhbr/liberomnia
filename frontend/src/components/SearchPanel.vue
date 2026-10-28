<script setup lang="ts">
import { computed, watch } from 'vue'
import { useSearchForm } from '../composables/useSearchForm'
import type { SearchMode } from '../composables/useSearchForm'
import type { AccessAvailability, LoadState, SearchRequest, SourcesResponse } from '../types/api'

const props = defineProps<{ catalog: LoadState<SourcesResponse>; busy?: boolean; canCancel?: boolean; cancelling?: boolean }>()
const emit = defineEmits<{ search: [request: SearchRequest]; cancel: [] }>()
const { form, period, rangeError, customizePeriod } = useSearchForm()
const modes: { value: SearchMode; label: string }[] = [
  { value: 'general', label: 'Pesquisa geral' }, { value: 'title', label: 'Título' },
  { value: 'author', label: 'Autor' }, { value: 'subject', label: 'Assunto' }, { value: 'passage', label: 'Trecho' },
]
const available = computed(() => props.catalog.status === 'success' && props.catalog.data.searchAvailable
  ? props.catalog.data.sources.filter(source => source.implementationStatus === 'available' && source.integrationType === 'api') : [])
watch(available, (sources, previous) => {
  form.sourceIds = form.sourceIds.filter(id => sources.some(source => source.id === id))
  if (!previous?.length && sources.length && !form.sourceIds.length) form.sourceIds = sources.map(source => source.id)
}, { immediate: true })
const canSearch = computed(() => available.value.length > 0 && form.sourceIds.length > 0 && !rangeError.value && !!form.query.trim() && !props.busy)
function submit() {
  if (!canSearch.value) return
  emit('search', { text: form.query.trim(), field: form.mode, sourceIds: [...form.sourceIds],
    language: form.language || null, availability: (form.availability || null) as AccessAvailability | null,
    publication: { from: form.from === '' ? null : Number(form.from), to: form.to === '' ? null : Number(form.to) },
    maxResultsPerSource: 40 })
}
</script>

<template>
  <form class="search-panel" aria-label="Pesquisa bibliográfica" @submit.prevent="submit">
    <fieldset class="search-modes">
      <legend class="sr-only">Pesquisar por</legend>
      <label v-for="mode in modes" :key="mode.value" class="mode-option" :class="{ 'mode-option--active': form.mode === mode.value }">
        <input v-model="form.mode" type="radio" name="search-mode" :value="mode.value" />{{ mode.label }}
      </label>
    </fieldset>
    <label class="sr-only" for="search-query">O que você deseja descobrir?</label>
    <div class="search-input-row">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6.5" /><path d="m16 16 5 5" /></svg>
      <input id="search-query" v-model="form.query" type="search" placeholder="Uma obra, um autor, uma nova descoberta…" aria-describedby="search-unavailable" />
      <button class="search-button" type="submit" :disabled="!canSearch" aria-describedby="search-unavailable">Pesquisar <span aria-hidden="true">↗</span></button>
    </div>
    <button v-if="busy || canCancel" class="text-button cancel-search" type="button" :disabled="cancelling" @click="emit('cancel')">{{ cancelling ? 'Cancelando…' : 'Cancelar pesquisa' }}</button>
    <p v-if="!available.length" id="search-unavailable" class="search-notice"><span aria-hidden="true">◷</span> Pesquisa temporariamente indisponível. Nenhuma fonte operacional no catálogo atual. Verifique a conexão e atualize o catálogo.</p>

    <details class="advanced-search">
      <summary>Pesquisa avançada <span class="summary-hint">Período, idioma e fontes</span></summary>
      <div class="advanced-content">
        <p class="filter-note">Datas da edição e disponibilidade são filtradas localmente, com cobertura incompleta. Trechos são excertos da fonte, sem confirmação de ocorrência ou página.</p>
        <div class="filter-grid">
          <div class="field"><label for="period">Período histórico</label>
            <select id="period" v-model="period">
              <option value="any">Todos os períodos</option><option value="nineteenth">Século XIX (1801–1900)</option>
              <option value="twentieth">Século XX (1901–2000)</option><option value="custom">Intervalo personalizado</option>
            </select>
          </div>
          <div class="field"><label for="language">Idioma da obra</label>
            <select id="language" v-model="form.language">
              <option value="">Todos os idiomas</option><option value="pt">Português</option><option value="en">Inglês</option>
              <option value="la">Latim</option><option value="fr">Francês</option><option value="es">Espanhol</option><option value="de">Alemão</option><option value="it">Italiano</option>
            </select>
          </div>
          <div class="field"><label for="availability">Disponibilidade</label><select id="availability" v-model="form.availability">
            <option value="">Qualquer disponibilidade</option><option value="viewable">Visualização integral ou parcial</option>
            <option value="pdf">PDF com link de download</option><option value="epub">EPUB com link de download</option>
            <option value="metadata_only">Somente metadados</option><option value="restricted">Acesso restrito</option><option value="unknown">Desconhecida</option>
          </select></div>
          <fieldset class="year-range"><legend>Intervalo de publicação</legend>
            <div class="field"><label for="year-from">De</label><input id="year-from" v-model="form.from" type="number" min="1" max="9999" step="1" placeholder="Ano inicial" :aria-invalid="!!rangeError" :aria-describedby="rangeError ? 'range-error' : undefined" @input="customizePeriod" /></div>
            <span aria-hidden="true">—</span>
            <div class="field"><label for="year-to">Até</label><input id="year-to" v-model="form.to" type="number" min="1" max="9999" step="1" placeholder="Ano final" :aria-invalid="!!rangeError" :aria-describedby="rangeError ? 'range-error' : undefined" @input="customizePeriod" /></div>
          </fieldset>
        </div>
        <p v-if="rangeError" id="range-error" class="field-error" role="alert">{{ rangeError }}</p>
        <fieldset class="source-selection"><legend>Fontes de pesquisa</legend>
          <p v-if="catalog.status === 'loading'">Carregando fontes…</p>
          <p v-else-if="catalog.status === 'error'">A seleção de fontes depende da conexão com a API.</p>
          <template v-else>
            <p v-if="catalog.data.sources.length === 0">Nenhuma fonte cadastrada.</p>
            <div v-else class="source-options">
              <label v-for="source in catalog.data.sources" :key="source.id">
                <input v-model="form.sourceIds" type="checkbox" :value="source.id" :disabled="!available.some(item => item.id === source.id)" />
                <span>{{ source.name }} <small>{{ source.implementationStatus === 'available' ? 'Disponível' : source.implementationStatus === 'temporarily_unavailable' ? 'Indisponível' : 'Planejada' }}</small></span>
              </label>
            </div>
          </template>
        </fieldset>
      </div>
    </details>
  </form>
</template>
