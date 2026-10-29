<script setup lang="ts">
import { sourceSearchAllowed } from './services/sourceStatus'
import { computed, nextTick, onBeforeUnmount, onMounted, provide, ref, watch } from 'vue'
import AppHeader from './components/AppHeader.vue'
import SidebarNavigation from './components/SidebarNavigation.vue'
import SearchPanel from './components/SearchPanel.vue'
import SearchFilters from './components/SearchFilters.vue'
import SearchResults from './components/SearchResults.vue'
import SourceCatalog from './components/SourceCatalog.vue'
import SourceStatusPanel from './components/SourceStatusPanel.vue'
import { useApplication } from './composables/useApplication'
import { searchFormKey, useSearchForm } from './composables/useSearchForm'
import { useSearchSession } from './composables/useSearchSession'
import type { SearchRequest } from './types/api'

const { session, records, busy, error, cancelling, start, cancel, resume } = useSearchSession()
const { health, catalog: apiCatalog, refresh } = useApplication()
const sourceOrder = ['google-books', 'internet-archive', 'open-library', 'gallica', 'annas-archive']
const catalog = computed(() => {
  if (apiCatalog.value.status !== 'success') return apiCatalog.value
  const rank = (id: string) => sourceOrder.includes(id) ? sourceOrder.indexOf(id) : sourceOrder.length
  return { ...apiCatalog.value, data: { ...apiCatalog.value.data,
    sources: [...apiCatalog.value.data.sources].sort((a, b) => rank(a.id) - rank(b.id)),
  } }
})
const searchForm = useSearchForm()
provide(searchFormKey, searchForm)
const panel = ref<InstanceType<typeof SearchPanel> | null>(null)
const active = ref('home')
const advancedOpen = ref(false)
const desktop = ref(false)
let media: MediaQueryList | undefined
const available = computed(() => catalog.value.status === 'success' && catalog.value.data.searchAvailable
  ? catalog.value.data.sources.filter(sourceSearchAllowed) : [])
const topActive = computed(() => active.value === 'sources' ? 'sources' : active.value === 'home' ? 'home' : 'search')
const sidebarFilters = computed(() => !!session.value && desktop.value)

async function navigate(section: string) {
  active.value = section
  if (section === 'passage') searchForm.form.mode = 'passage'
  if (section === 'advanced') advancedOpen.value = true
  await nextTick()
  if (section === 'search' || section === 'passage') document.getElementById('search-query')?.focus()
  else if (section === 'advanced') document.querySelector<HTMLElement>('#advanced summary')?.focus()
}
function openAdvanced() {
  void navigate('advanced')
  void nextTick(() => document.getElementById('advanced')?.scrollIntoView?.({ block: 'nearest' }))
}
function historicalSearch() { searchForm.period.value = 'nineteenth'; openAdvanced() }
function submitSearch(request: SearchRequest) { active.value = 'search'; void start(request) }
function quickSearch() { void navigate('search'); panel.value?.submit() }
function updateViewport() { desktop.value = media?.matches ?? false }
function readHash() {
  const section = window.location.hash.slice(1)
  if (['home', 'search', 'sources', 'passage', 'advanced', 'research-guide'].includes(section)) void navigate(section)
}
watch(() => session.value?.finished, finished => { if (finished) void refresh() })
watch(session, (value, previous) => { if (value && value.id !== previous?.id && desktop.value) advancedOpen.value = true })
onMounted(() => {
  media = window.matchMedia?.('(min-width: 1100px)')
  updateViewport()
  media?.addEventListener('change', updateViewport)
  window.addEventListener('hashchange', readHash)
  readHash()
})
onBeforeUnmount(() => { media?.removeEventListener('change', updateViewport); window.removeEventListener('hashchange', readHash) })
</script>

<template>
  <a class="skip-link" href="#main">Pular para o conteúdo</a>
  <div class="site-shell">
    <AppHeader v-model:query="searchForm.form.query" :catalog="catalog" :health="health" :active="topActive" :can-search="panel?.canSearch ?? false" @navigate="navigate" @search="quickSearch" />
    <div v-if="health.status === 'error'" class="connection-error" role="alert">
      <span>{{ health.message }}</span><button class="text-button" type="button" @click="refresh">Reconectar</button>
    </div>
    <main id="main" class="workspace" :class="{ 'workspace--results': !!session }" tabindex="-1">
      <aside class="workspace-sidebar" aria-label="Navegação e filtros">
        <SidebarNavigation :active="active" :available="available" @navigate="navigate" />
        <SearchFilters v-if="sidebarFilters" v-model:open="advancedOpen" :state="searchForm" :catalog="catalog" :available="available" />
      </aside>
      <div class="workspace-center">
        <SearchPanel ref="panel" :catalog="catalog" :busy="busy" :cancelling="cancelling" :can-cancel="!!session && !session.finished" :advanced-open="advancedOpen" @search="submitSearch" @cancel="cancel" @advanced="openAdvanced" />
        <SearchFilters v-if="!sidebarFilters" v-model:open="advancedOpen" :state="searchForm" :catalog="catalog" :available="available" />
        <SearchResults :session="session" :records="records" :busy="busy" :error="error" @resume="resume" />
        <section id="research-guide" class="panel research-guide" aria-labelledby="guide-title">
          <h2 id="guide-title">Guia de pesquisa</h2>
          <dl class="research-modes">
            <div><dt>Obras e autores</dt><dd>Use a pesquisa geral ou escolha um campo para refinar a consulta.</dd></div>
            <div><dt>Assuntos</dt><dd>Explore temas pelos metadados e categorias informados pelas bibliotecas.</dd></div>
            <div><dt>Trechos</dt><dd>Procure uma frase e consulte os excertos disponibilizados pela fonte.</dd></div>
          </dl>
          <a href="#passage" @click="navigate('passage')">Abrir pesquisa por trecho <span aria-hidden="true">→</span></a>
        </section>
        <section class="panel period-guide" aria-labelledby="period-title">
          <h2 id="period-title">Períodos e edições</h2>
          <p>Investigue a publicação de uma edição, preservando a diferença entre a data do exemplar e a origem da obra.</p>
          <button type="button" class="text-button" @click="historicalSearch">Aplicar século XIX · 1801–1900</button>
        </section>
      </div>
      <aside class="workspace-info" aria-label="Acervos e informações de pesquisa">
        <SourceStatusPanel v-if="session" :session="session" :catalog="catalog" />
        <SourceCatalog :state="catalog" @retry="refresh" />
        <section class="panel notice-panel" aria-labelledby="coverage-title">
          <h2 id="coverage-title">Sobre a cobertura</h2>
          <p>As consultas recuperam um conjunto limitado de registros. Filtros locais e buscas alternativas podem deixar obras de fora.</p>
          <p>Nenhum resultado não significa ausência no acervo.</p>
        </section>
        <section class="panel reading-guide" aria-labelledby="reading-title">
          <h2 id="reading-title">Leitura e acesso</h2>
          <ul class="information-list">
            <li><strong>Visualização</strong><span>Pode ser integral ou parcial, conforme a fonte.</span></li>
            <li><strong>PDF e EPUB</strong><span>Links aparecem apenas quando informados e disponíveis.</span></li>
            <li><strong>Direitos de acesso</strong><span>Podem variar por país. Um livro visível não implica download livre.</span></li>
          </ul>
        </section>
      </aside>
    </main>
    <footer class="site-footer">
      <p><strong>LiberOmnia</strong> · Pesquisa bibliográfica federada.</p>
      <p>Dados e disponibilidade preservados conforme a fonte. <a href="https://github.com/auhauhbr/liberomnia" target="_blank" rel="noopener noreferrer">Sobre o projeto ↗</a></p>
    </footer>
  </div>
</template>
