<script setup lang="ts">
import { useSearchForm } from '../composables/useSearchForm'
import type { SearchMode } from '../composables/useSearchForm'
import type { LoadState, SourcesResponse } from '../types/api'

defineProps<{ catalog: LoadState<SourcesResponse> }>()
const { form, period, rangeError, customizePeriod } = useSearchForm()
const modes: { value: SearchMode; label: string }[] = [
  { value: 'general', label: 'Pesquisa geral' }, { value: 'title', label: 'Título' },
  { value: 'author', label: 'Autor' }, { value: 'passage', label: 'Trecho' },
]
</script>

<template>
  <form class="search-panel" aria-label="Pesquisa bibliográfica" @submit.prevent>
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
      <button class="search-button" type="submit" disabled aria-describedby="search-unavailable">Pesquisar <span aria-hidden="true">↗</span></button>
    </div>
    <p id="search-unavailable" class="search-notice"><span aria-hidden="true">◷</span> Pesquisa temporariamente indisponível. Estamos preparando as integrações com os acervos.</p>

    <details class="advanced-search">
      <summary>Pesquisa avançada <span class="summary-hint">Período, idioma e fontes</span></summary>
      <div class="advanced-content">
        <p class="filter-note">Explore os filtros previstos. A disponibilidade de cada filtro dependerá da fonte consultada.</p>
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
              <option value="fr">Francês</option><option value="es">Espanhol</option><option value="de">Alemão</option><option value="it">Italiano</option>
            </select>
          </div>
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
                <input v-model="form.sourceIds" type="checkbox" :value="source.id" :disabled="source.implementationStatus !== 'available'" />
                <span>{{ source.name }} <small>{{ source.implementationStatus === 'available' ? 'Disponível' : source.implementationStatus === 'temporarily_unavailable' ? 'Indisponível' : 'Planejada' }}</small></span>
              </label>
            </div>
          </template>
        </fieldset>
      </div>
    </details>
  </form>
</template>
