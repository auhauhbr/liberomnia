<script setup lang="ts">
import { computed } from 'vue'
import type { BibliographicRecord } from '../types/api'
import { externalUrl } from '../services/search'

const props = defineProps<{ record: BibliographicRecord; local?: boolean; fallback?: boolean }>()
const labels = { viewable: 'Visualização', pdf: 'Download PDF', epub: 'Download EPUB', restricted: 'Acesso restrito',
  metadata_only: 'Somente metadados', unknown: 'Disponibilidade desconhecida' }
const links = computed(() => props.record.access.map(access => ({ ...access,
  safeUrl: access.availability === 'restricted' ? null : externalUrl(access.url),
  label: access.viewability === 'ALL_PAGES' ? 'Visualização integral'
    : access.viewability === 'PARTIAL' ? 'Visualização parcial' : labels[access.availability],
})))
</script>

<template>
  <article class="bibliographic-card" :class="{ 'bibliographic-card--local': local }">
    <div class="result-tags"><span>{{ record.sourceId === 'google-books' ? 'Google Books' : record.sourceId }}</span>
      <span v-if="local">Filtrado localmente</span><span v-if="fallback">Fallback · correspondência nos metadados</span>
    </div>
    <h3>{{ record.title }}</h3><p v-if="record.subtitle" class="book-subtitle">{{ record.subtitle }}</p>
    <p>{{ record.authors.length ? record.authors.join(' · ') : 'Autoria não informada' }}</p>
    <dl class="book-metadata">
      <div><dt>Editora</dt><dd>{{ record.publisher ?? 'Não informada' }}</dd></div>
      <div><dt>Ano da edição</dt><dd>{{ record.publicationYear ?? 'Não informado' }}</dd></div>
      <div><dt>Idioma</dt><dd>{{ record.language ?? 'Não informado' }}</dd></div>
    </dl>
    <p v-if="record.identifiers.length" class="book-identifiers">{{ record.identifiers.map(id => `${id.scheme}: ${id.value}`).join(' · ') }}</p>
    <ul class="book-access">
      <li v-for="(access, index) in links" :key="index">
        <a v-if="access.safeUrl" :href="access.safeUrl" target="_blank" rel="noopener noreferrer">{{ access.label }} ↗</a>
        <span v-else>{{ access.label }}{{ access.format ? ` (${access.format.toUpperCase()})` : '' }}</span>
      </li>
    </ul>
    <p class="access-note">PDF: {{ record.pdfAvailable === false ? 'indisponível' : record.pdfAvailable === true ? 'informado pela fonte; consulte o acesso acima' : 'não informado' }}.
      EPUB: {{ record.epubAvailable === false ? 'indisponível' : record.epubAvailable === true ? 'informado pela fonte; consulte o acesso acima' : 'não informado' }}.</p>
    <p v-if="record.accessCountry" class="access-note">Disponibilidade informada para {{ record.accessCountry }}; pode variar por país.</p>
    <p v-if="record.publicDomain === true" class="access-note">Domínio público segundo a fonte, no país informado.</p>
    <p><a v-if="externalUrl(record.originalUrl)" :href="externalUrl(record.originalUrl)!" target="_blank" rel="noopener noreferrer">Ver na origem ↗</a>
      <a v-if="externalUrl(record.infoUrl) && record.infoUrl !== record.originalUrl" :href="externalUrl(record.infoUrl)!" target="_blank" rel="noopener noreferrer">Informações na fonte ↗</a>
      <a v-if="externalUrl(record.previewUrl) && !links.some(access => access.safeUrl === record.previewUrl)" :href="externalUrl(record.previewUrl)!" target="_blank" rel="noopener noreferrer">Página de visualização na fonte ↗</a>
    </p>
    <div v-for="(evidence, index) in record.textualEvidence" :key="index" class="source-excerpt">
      <p>Excerto fornecido pela fonte — ocorrência e página não verificadas.</p>
      <blockquote v-if="evidence.text">{{ evidence.text }}</blockquote>
    </div>
  </article>
</template>
