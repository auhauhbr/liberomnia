<script setup lang="ts">
import { sourceStatusLabel } from '../services/sourceStatus'
import type { LoadState, SourcesResponse } from '../types/api'
defineProps<{ catalog: LoadState<SourcesResponse> }>()
defineEmits<{ navigate: [] }>()
</script>

<template>
  <div class="source-strip" aria-label="Integrações bibliográficas" :aria-busy="catalog.status === 'loading'">
    <p v-if="catalog.status === 'loading'" class="strip-message" role="status">Carregando integrações bibliográficas…</p>
    <p v-else-if="catalog.status === 'error'" class="strip-message">Catálogo indisponível. Consulte o estado das bibliotecas abaixo.</p>
    <template v-else>
      <p v-if="!catalog.data.sources.length" class="strip-message">Nenhuma fonte cadastrada.</p>
      <a v-for="source in catalog.data.sources" :key="source.id" href="#sources" class="source-strip-item" :data-source="source.id" @click="$emit('navigate')">
        <strong>{{ source.name }}</strong>
        <span><span class="strip-indicator" aria-hidden="true">{{ (source.operational ? source.operational.state === 'available' : source.implementationStatus === 'available') ? '●' : '○' }}</span>
          {{ source.integrationType === 'external' ? 'Externa · ' : 'API · ' }}{{ sourceStatusLabel(source).toLocaleLowerCase('pt-BR') }}</span>
      </a>
    </template>
  </div>
</template>
