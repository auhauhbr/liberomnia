<script setup lang="ts">
import { sourceStatusLabel, diagnosticMessage } from '../services/sourceStatus'
import type { LoadState, SourcesResponse } from '../types/api'
import StatusBadge from './StatusBadge.vue'

defineProps<{ state: LoadState<SourcesResponse> }>()
defineEmits<{ retry: [] }>()
const labels: Record<string, string> = {
  general: 'Geral', title: 'Título', author: 'Autor', subject: 'Assunto', language: 'Idioma',
  availability: 'Disponibilidade', publicationRange: 'Período', passage: 'Trecho', externalSearch: 'Pesquisa externa',
}
</script>

<template>
  <section id="sources" class="panel sources-section" aria-labelledby="sources-title" :aria-busy="state.status === 'loading'">
    <div class="section-heading"><h2 id="sources-title">Estado das bibliotecas</h2>
      <button class="text-button" type="button" :disabled="state.status === 'loading'" @click="$emit('retry')">Atualizar catálogo</button>
    </div>
    <p v-if="state.status === 'loading'" class="state-box" role="status">Carregando catálogo de fontes…</p>
    <div v-else-if="state.status === 'error'" class="state-box state-box--error" role="alert">
      <p>{{ state.message }}</p><button class="text-button" type="button" @click="$emit('retry')">Tentar carregar novamente</button>
    </div>
    <template v-else>
      <p v-if="!state.data.sources.length" class="state-box">Nenhuma fonte cadastrada no momento.</p>
      <ul v-else class="source-grid">
        <li v-for="source in state.data.sources" :key="source.id" class="source-card">
          <div class="source-topline"><h3>{{ source.name }}</h3>
            <StatusBadge :tone="(source.operational ? source.operational.state === 'available' : source.implementationStatus === 'available') ? 'success' : source.implementationStatus === 'temporarily_unavailable' ? 'warning' : 'neutral'">
              {{ sourceStatusLabel(source) }}
            </StatusBadge>
          </div>
          <p v-if="source.operational" class="filter-note">Integração implementada · {{ source.operational.configured ? 'configurada' : 'configuração pendente' }}.
            <span v-if="source.operational.state === 'unverified'">A primeira pesquisa verificará o serviço externo.</span>
            <span v-if="source.operational.lastSuccessfulAt">Último sucesso: {{ new Date(source.operational.lastSuccessfulAt * 1000).toLocaleString('pt-BR') }}.</span>
            <span v-if="source.operational.error">{{ diagnosticMessage(source.operational.error) }}</span>
            <span v-if="source.operational.searchAllowed && source.operational.error">Uma nova pesquisa está permitida.</span>
          </p>
          <div class="source-bottomline"><p class="source-integration">{{ source.integrationType === 'external' ? 'Integração externa' : 'Integração via API' }}</p>
            <details class="source-details"><summary><span class="sr-only">{{ source.name }}: </span>Escopo e filtros</summary>
              <ul class="capabilities" :aria-label="`Capacidades de ${source.name}`">
                <li v-for="capability in source.capabilities" :key="capability.id">{{ labels[capability.id] ?? capability.id }}
                  <span class="sr-only"> — {{ capability.status === 'confirmed' ? 'confirmada' : 'planejada' }}</span>
                  <span v-if="capability.status === 'confirmed'" aria-hidden="true"> ✓</span>
                </li>
              </ul><p lang="en">{{ source.notes }}</p>
            </details>
          </div>
        </li>
      </ul>
      <p class="catalog-note">Somente fontes habilitadas para pesquisa podem ser consultadas. Capacidades sem ✓ permanecem planejadas.</p>
    </template>
  </section>
</template>
