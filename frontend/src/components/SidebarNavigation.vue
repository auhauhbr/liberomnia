<script setup lang="ts">
import { ref } from 'vue'
import type { Source } from '../types/api'
defineProps<{ active: string; available: Source[] }>()
const emit = defineEmits<{ navigate: [section: string] }>()
const expanded = ref(false)
const entries = [
  { id: 'home', label: 'Início' }, { id: 'search', label: 'Pesquisar livros' },
  { id: 'passage', label: 'Pesquisa por trecho' }, { id: 'sources', label: 'Bibliotecas' }, { id: 'advanced', label: 'Pesquisa avançada' },
]
function navigate(section: string) { expanded.value = false; emit('navigate', section) }
</script>

<template>
  <div class="sidebar-navigation">
    <button class="sidebar-toggle" type="button" :aria-expanded="expanded" aria-controls="sidebar-menu" @click="expanded = !expanded">Menu de navegação <span aria-hidden="true">☰</span></button>
    <div id="sidebar-menu" class="sidebar-content" :class="{ 'is-open': expanded }">
      <nav aria-label="Navegação lateral"><h2>LiberOmnia</h2>
        <a v-for="entry in entries" :key="entry.id" :href="`#${entry.id}`" :aria-current="active === entry.id ? 'location' : undefined" @click="navigate(entry.id)">{{ entry.label }}</a>
      </nav>
      <div class="sidebar-group"><h3>Fontes disponíveis</h3>
        <p v-if="!available.length" class="muted">Nenhuma fonte operacional.</p>
        <a v-for="source in available" :key="source.id" href="#advanced" @click="navigate('advanced')"><span class="availability-mark" aria-hidden="true">●</span> {{ source.name }}</a>
      </div>
      <div class="sidebar-group"><h3>O projeto</h3>
        <a href="https://github.com/auhauhbr/liberomnia" target="_blank" rel="noopener noreferrer">Código-fonte <span aria-hidden="true">↗</span></a>
        <a href="#research-guide" @click="navigate('research-guide')">Como pesquisar</a>
      </div>
    </div>
  </div>
</template>
