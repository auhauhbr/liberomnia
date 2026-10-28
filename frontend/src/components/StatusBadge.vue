<script setup lang="ts">
defineProps<{ tone?: 'neutral' | 'success' | 'warning' | 'error' }>()
</script>

<template>
  <span class="status-badge" :class="`status-badge--${tone ?? 'neutral'}`">
    <span class="status-dot" aria-hidden="true"></span><slot />
  </span>
</template>
