<script setup lang="ts">
import type { LoadState, SourcesResponse } from '../types/api'
import StatusBadge from './StatusBadge.vue'

defineProps<{ state: LoadState<SourcesResponse> }>()
defineEmits<{ retry: [] }>()

const labels: Record<string, string> = {
  general: 'Geral', title: 'Título', author: 'Autor', subject: 'Assunto',
  language: 'Idioma', availability: 'Disponibilidade', publicationRange: 'Período', passage: 'Trecho', externalSearch: 'Pesquisa externa',
}
</script>

<template>
  <section id="sources" class="sources-section" aria-labelledby="sources-title" :aria-busy="state.status === 'loading'">
    <div class="section-heading">
      <div><p class="eyebrow">UM HORIZONTE DE ACERVOS</p><h2 id="sources-title">Bibliotecas, em conexão.</h2></div>
      <button class="text-button" type="button" @click="$emit('retry')">Atualizar catálogo</button>
    </div>
    <p class="section-description">Conheça as fontes disponíveis e planejadas. Cada acervo tem sua própria cobertura e possibilidades de pesquisa.</p>
    <p v-if="state.status === 'loading'" class="state-box" role="status">Carregando catálogo de fontes…</p>
    <div v-else-if="state.status === 'error'" class="state-box state-box--error" role="alert">
      <p>{{ state.message }}</p><button class="text-button" type="button" @click="$emit('retry')">Tentar carregar novamente</button>
    </div>
    <template v-else>
      <p v-if="state.data.sources.length === 0" class="state-box">Nenhuma fonte cadastrada no momento.</p>
      <ul v-else class="source-grid">
        <li v-for="(source, index) in state.data.sources" :key="source.id" class="source-card">
          <div class="source-topline"><span class="source-number" aria-hidden="true">{{ String(index + 1).padStart(2, '0') }}</span>
            <StatusBadge :tone="source.implementationStatus === 'available' ? 'success' : 'neutral'">
              {{ source.implementationStatus === 'available' ? 'Disponível' : source.implementationStatus === 'temporarily_unavailable' ? 'Indisponível' : 'Planejada' }}
            </StatusBadge>
          </div>
          <h3>{{ source.name }}</h3>
          <p class="source-integration">{{ source.integrationType === 'external' ? 'Integração externa' : 'Integração via API' }}</p>
          <ul class="capabilities" :aria-label="`Capacidades de ${source.name}`">
            <li v-for="capability in source.capabilities" :key="capability.id">
              {{ labels[capability.id] ?? capability.id }}
              <span class="sr-only"> — {{ capability.status === 'confirmed' ? 'confirmada' : 'planejada' }}</span>
              <span v-if="capability.status === 'confirmed'" aria-hidden="true"> ✓</span>
            </li>
          </ul>
          <details class="source-details"><summary>Escopo da integração</summary><p lang="en">{{ source.notes }}</p></details>
        </li>
      </ul>
      <p class="catalog-note">Capacidades sem ✓ são planejadas e ainda precisam de validação. Somente fontes disponíveis podem ser consultadas.</p>
    </template>
  </section>
</template>
