<script setup lang="ts">
import { sourceStatusLabel } from '../services/sourceStatus'
import type { SearchFormState } from '../composables/useSearchForm'
import type { LoadState, Source, SourcesResponse } from '../types/api'

const props = defineProps<{ state: SearchFormState; catalog: LoadState<SourcesResponse>; available: Source[]; open: boolean }>()
const emit = defineEmits<{ 'update:open': [value: boolean] }>()
const { form, period, rangeError, customizePeriod } = props.state
function onToggle(event: Event) { emit('update:open', (event.target as HTMLDetailsElement).open) }
</script>

<template>
    <details id="advanced" class="panel advanced-search" :open="open" @toggle="onToggle">
      <summary>Filtros avançados <span class="summary-hint">Período, idioma e fontes</span></summary>
      <div class="advanced-content">
        <p class="filter-note">Filtre os registros recuperados. O período se refere à edição, não à publicação original da obra.</p>
        <div class="filter-grid">
          <div class="field"><label for="period">Período histórico</label>
            <select form="bibliographic-search" id="period" v-model="period">
              <option value="any">Todos os períodos</option><option value="nineteenth">Século XIX (1801–1900)</option>
              <option value="twentieth">Século XX (1901–2000)</option><option value="custom">Intervalo personalizado</option>
            </select>
          </div>
          <div class="field"><label for="language">Idioma da obra</label>
            <select form="bibliographic-search" id="language" v-model="form.language">
              <option value="">Todos os idiomas</option><option value="pt">Português</option><option value="en">Inglês</option>
              <option value="la">Latim</option><option value="fr">Francês</option><option value="es">Espanhol</option><option value="de">Alemão</option><option value="it">Italiano</option>
            </select>
          </div>
          <div class="field"><label for="availability">Disponibilidade</label><select form="bibliographic-search" id="availability" v-model="form.availability">
            <option value="">Qualquer disponibilidade</option><option value="viewable">Visualização integral ou parcial</option>
            <option value="pdf">PDF com link de download</option><option value="epub">EPUB com link de download</option>
            <option value="metadata_only">Somente metadados</option><option value="restricted">Acesso restrito</option><option value="unknown">Desconhecida</option>
          </select></div>
          <fieldset class="year-range"><legend>Intervalo de publicação</legend>
            <div class="field"><label for="year-from">De</label><input form="bibliographic-search" id="year-from" v-model="form.from" type="number" min="1" max="9999" step="1" placeholder="Ano inicial" :aria-invalid="!!rangeError" :aria-describedby="rangeError ? 'range-error' : undefined" @input="customizePeriod" /></div>
            <span aria-hidden="true">—</span>
            <div class="field"><label for="year-to">Até</label><input form="bibliographic-search" id="year-to" v-model="form.to" type="number" min="1" max="9999" step="1" placeholder="Ano final" :aria-invalid="!!rangeError" :aria-describedby="rangeError ? 'range-error' : undefined" @input="customizePeriod" /></div>
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
                <input form="bibliographic-search" v-model="form.sourceIds" type="checkbox" :value="source.id" :disabled="!available.some(item => item.id === source.id)" />
                <span>{{ source.name }} <small>{{ sourceStatusLabel(source) }}</small></span>
              </label>
            </div>
          </template>
        </fieldset>
      </div>
    </details>
</template>
