<script setup lang="ts">
import { computed } from 'vue'
import SearchPanel from './components/SearchPanel.vue'
import SourceCatalog from './components/SourceCatalog.vue'
import StatusBadge from './components/StatusBadge.vue'
import { useApplication } from './composables/useApplication'

const { health, catalog, refresh } = useApplication()
const healthLabel = computed(() => health.value.status === 'success' ? 'API conectada'
  : health.value.status === 'error' ? 'API indisponível' : 'Conectando à API…')
</script>

<template>
  <a class="skip-link" href="#main">Pular para o conteúdo</a>
  <div class="site-shell">
    <header class="site-header">
      <a class="brand" href="#main" aria-label="LiberOmnia, início">
        <svg class="brand-mark" viewBox="0 0 48 48" fill="none" aria-hidden="true"><rect width="48" height="48" rx="12" fill="currentColor" /><path d="M9 12c6-2 10-1 15 2 5-3 9-4 15-2v23c-6-2-10-1-15 2-5-3-9-4-15-2zM24 14v23" stroke="#f6f2e8" stroke-width="2" stroke-linejoin="round" /></svg>
        <span>Liber<span class="brand-light">Omnia</span><small>CONHECIMENTO SEM FRONTEIRAS</small></span>
      </a>
      <nav aria-label="Navegação principal"><a href="#sources">Os acervos</a><a href="#about">O projeto <span aria-hidden="true">↗</span></a></nav>
      <span class="development-label">Em desenvolvimento</span>
    </header>

    <main id="main" tabindex="-1">
      <section class="hero" aria-labelledby="hero-title">
        <div class="hero-decoration" aria-hidden="true"><span></span><span></span><span></span><span></span><span></span></div>
        <p class="eyebrow">MUITAS BIBLIOTECAS. UM NOVO PONTO DE PARTIDA.</p>
        <h1 id="hero-title">O conhecimento não cabe<br />em uma só <em>estante.</em></h1>
        <p class="hero-description">Um lugar para descobrir obras, conectar ideias e explorar<br class="desktop-break" /> o patrimônio escrito em diferentes bibliotecas digitais.</p>
        <SearchPanel :catalog="catalog" />
        <div class="hero-footnote"><span class="small-rule"></span><span>Pesquisa aberta. Descobertas compartilhadas.</span><span class="small-rule"></span></div>
      </section>

      <SourceCatalog :state="catalog" @retry="refresh" />

      <section id="about" class="about-section" aria-labelledby="about-title">
        <div class="about-symbol" aria-hidden="true">L<span>O</span></div>
        <div><p class="eyebrow">CONSTRUÍDO PARA A CURIOSIDADE</p><h2 id="about-title">Uma ponte entre você e os acervos.</h2>
          <p>O LiberOmnia é um projeto open source de pesquisa bibliográfica federada. Nossa proposta é reunir descobertas, respeitando as particularidades de cada biblioteca e preservando os dados de cada edição.</p>
        </div>
        <a class="project-link" href="https://github.com/auhauhbr/liberomnia">Conheça o projeto <span aria-hidden="true">↗</span></a>
      </section>
    </main>

    <footer class="site-footer">
      <p><strong>LiberOmnia</strong> <span>Um projeto aberto ao conhecimento.</span></p>
      <div class="application-status" role="status" aria-live="polite">
        <StatusBadge :tone="health.status === 'success' ? 'success' : health.status === 'error' ? 'error' : 'neutral'">{{ healthLabel }}</StatusBadge>
        <span>Fundação · v0.1.0</span>
      </div>
    </footer>
    <div v-if="health.status === 'error'" class="connection-error" role="alert">
      <span>{{ health.message }}</span><button class="text-button" type="button" @click="refresh">Reconectar</button>
    </div>
  </div>
</template>
