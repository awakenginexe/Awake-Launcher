<script setup lang="ts">
withDefaults(defineProps<{ label: string; rows?: number }>(), { rows: 3 });
</script>

<template>
  <div class="loading-skeleton" role="status" aria-busy="true">
    <span class="visually-hidden">{{ label }}</span>
    <div v-for="row in rows" :key="row" class="loading-skeleton-row shimmer" aria-hidden="true"></div>
  </div>
</template>

<style scoped>
.loading-skeleton { display: grid; gap: 8px; width: 100%; }
.loading-skeleton-row { height: 36px; border-radius: 8px; }
</style>
