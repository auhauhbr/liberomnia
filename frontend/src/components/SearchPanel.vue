<script setup lang="ts">
import { computed, inject, watch } from 'vue'
import { searchFormKey, useSearchForm } from '../composables/useSearchForm'
import type { SearchMode } from '../composables/useSearchForm'
import type { AccessAvailability, LoadState, SearchRequest, SourcesResponse } from '../types/api'

const props = defineProps<{ catalog: LoadState<SourcesResponse>; busy?: boolean; canCancel?: boolean; cancelling?: boolean; advancedOpen?: boolean }>()
const emit = defineEmits<{ search: [request: SearchRequest]; cancel: []; advanced: [] }>()
const { form, rangeError } = inject(searchFormKey, useSearchForm, true)
const modes: { value: SearchMode; label: string }[] = [
  { value: 'general', label: 'Geral' }, { value: 'title', label: 'Título' },
  { value: 'author', label: 'Autor' }, { value: 'subject', label: 'Assunto' }, { value: 'passage', label: 'Trecho' },
]
const available = computed(() => props.catalog.status === 'success' && props.catalog.data.searchAvailable
  ? props.catalog.data.sources.filter(source => source.implementationStatus === 'available' && source.integrationType === 'api') : [])
let selectionInitialized = false
watch(available, sources => {
  if (props.catalog.status !== 'success') return
  form.sourceIds = form.sourceIds.filter(id => sources.some(source => source.id === id))
  if (!selectionInitialized && sources.length) {
    form.sourceIds = sources.map(source => source.id)
    selectionInitialized = true
  }
}, { immediate: true })
const selectedNames = computed(() => available.value.filter(source => form.sourceIds.includes(source.id)).map(source => source.name).join(', '))
const canSearch = computed(() => available.value.length > 0 && form.sourceIds.length > 0 && !rangeError.value && !!form.query.trim() && !props.busy)
function submit() {
  if (!canSearch.value) return
  emit('search', { text: form.query.trim(), field: form.mode, sourceIds: [...form.sourceIds],
    language: form.language || null, availability: (form.availability || null) as AccessAvailability | null,
    publication: { from: form.from === '' ? null : Number(form.from), to: form.to === '' ? null : Number(form.to) },
    maxResultsPerSource: 40 })
}
defineExpose({ submit, canSearch })
</script>

<template>
  <section id="search" class="panel search-panel" aria-labelledby="search-title">
    <div class="panel-heading"><h2 id="search-title">Pesquisar no acervo</h2><span class="muted small">Pesquisa bibliográfica</span></div>
    <form id="bibliographic-search" aria-label="Pesquisa bibliográfica" @submit.prevent="submit">
      <fieldset class="search-modes">
        <legend class="sr-only">Pesquisar por</legend>
        <label v-for="mode in modes" :key="mode.value" class="mode-option" :class="{ 'mode-option--active': form.mode === mode.value }">
          <input v-model="form.mode" type="radio" name="search-mode" :value="mode.value" />{{ mode.label }}
        </label>
      </fieldset>
      <label id="passage" for="search-query">{{ form.mode === 'passage' ? 'Trecho ou frase a pesquisar' : 'Título, autor ou assunto' }}</label>
      <div class="search-input-row" :class="{ 'search-input-row--passage': form.mode === 'passage' }">
        <textarea v-if="form.mode === 'passage'" id="search-query" v-model="form.query" rows="4" maxlength="4096" placeholder="Digite a frase que deseja pesquisar…" aria-describedby="passage-hint" />
        <input v-else id="search-query" v-model="form.query" type="search" maxlength="4096" placeholder="O que você procura?" :aria-describedby="!available.length ? 'search-unavailable' : undefined" />
        <button class="search-button" type="submit" :disabled="!canSearch">{{ busy ? 'Pesquisando…' : 'Pesquisar' }}</button>
      </div>
      <p v-if="form.mode === 'passage'" id="passage-hint" class="filter-note">Frases são pesquisadas entre aspas. Um excerto não comprova a ocorrência nem a página do trecho.</p>
      <div class="search-toolbar">
        <button type="button" class="text-button" aria-controls="advanced" :aria-expanded="!!advancedOpen" @click="emit('advanced')">Filtros avançados</button>
        <button v-if="busy || canCancel" class="text-button cancel-search" type="button" :disabled="cancelling" @click="emit('cancel')">{{ cancelling ? 'Cancelando…' : 'Cancelar pesquisa' }}</button>
      </div>
      <p class="selected-sources">Fontes selecionadas: <strong>{{ selectedNames || 'nenhuma fonte operacional selecionada' }}</strong>.</p>
      <p v-if="rangeError" class="field-error" role="alert">{{ rangeError }} Corrija o intervalo nos filtros avançados.</p>
      <p v-if="!available.length" id="search-unavailable" class="search-notice" role="status">Pesquisa temporariamente indisponível. Nenhuma fonte operacional no catálogo atual. Verifique a conexão e atualize o catálogo.</p>
    </form>
  </section>
</template>
