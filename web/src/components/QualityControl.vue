<script setup lang="ts">
import AppSelect from './AppSelect.vue';
defineProps<{ value: string; disabled?: boolean; id: string }>();
const emit = defineEmits<{ change: [value: string] }>();
</script>
<template>
  <div class="quality-control">
    <label :for="id">Качество видео <span>Отправка и приём</span></label>
    <AppSelect
      :id="id"
      label="Качество видео"
      :model-value="value"
      :disabled="disabled"
      :options="[
        { value: 'auto', label: 'Авто · по соединению' },
        { value: '360', label: 'До 360p · экономия' },
        { value: '720', label: 'До 720p · HD' },
        { value: '1080', label: 'До 1080p · Full HD' },
      ]"
      @update:model-value="emit('change', $event)"
    />
  </div>
</template>
