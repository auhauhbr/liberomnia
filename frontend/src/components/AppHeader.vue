<script setup lang="ts">
import type { HealthResponse, LoadState, SourcesResponse } from '../types/api'
import SourceStrip from './SourceStrip.vue'
import StatusBadge from './StatusBadge.vue'

defineProps<{ catalog: LoadState<SourcesResponse>; health: LoadState<HealthResponse>; active: string; canSearch: boolean }>()
const query = defineModel<string>('query', { required: true })
defineEmits<{ navigate: [section: string]; search: [] }>()
</script>

<template>
  <header id="home" class="site-header">
    <div class="header-topline">
      <h1><a class="brand" href="#home" @click="$emit('navigate', 'home')">LiberOmnia</a></h1>
      <StatusBadge :tone="health.status === 'success' ? 'success' : health.status === 'error' ? 'error' : 'neutral'">
        {{ health.status === 'success' ? 'API conectada' : health.status === 'error' ? 'API indisponível' : 'Conectando à API…' }}
      </StatusBadge>
    </div>
    <p class="header-subtitle" lang="en">Discover books across digital libraries.</p>
    <p class="header-description">Pesquisa bibliográfica em um só lugar, com a identidade e os limites de cada acervo preservados.</p>
    <SourceStrip :catalog="catalog" @navigate="$emit('navigate', 'sources')" />
    <div class="header-note"><span class="note-marker" aria-hidden="true">i</span> Fontes independentes. Metadados, leitura e downloads conforme a disponibilidade na origem.</div>
    <div class="top-navigation">
      <nav aria-label="Navegação principal">
        <a v-for="item in [{ id: 'home', label: 'Início' }, { id: 'search', label: 'Pesquisa' }, { id: 'sources', label: 'Acervos' }]" :key="item.id"
          :href="`#${item.id}`" :aria-current="active === item.id ? 'location' : undefined" @click="$emit('navigate', item.id)">{{ item.label }}</a>
      </nav>
      <form class="quick-search" role="search" aria-label="Pesquisa rápida" @submit.prevent="$emit('search')">
        <label class="sr-only" for="quick-query">Pesquisa rápida</label>
        <input id="quick-query" v-model="query" type="search" maxlength="4096" placeholder="Título, autor ou assunto…" />
        <button type="submit" :disabled="!canSearch" aria-label="Executar pesquisa rápida">Buscar</button>
      </form>
    </div>
  </header>
</template>
