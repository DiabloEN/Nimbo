<script setup lang="ts">
import { computed } from 'vue';
import AppSelect from './AppSelect.vue';
import { isLocalNoiseEngine } from '../composables/audioLab';
import type { useAudioDevices } from '../composables/useAudioDevices';

const props = defineProps<{ devices: ReturnType<typeof useAudioDevices>; disabled: boolean }>();
const lab = props.devices.lab;
const mode = computed(() => (lab.suppression.value ? lab.engine.value : 'off'));
const options = [
  { value: 'gtcrn', label: 'GTCRN' },
  { value: 'rnnoise', label: 'RNNoise' },
  { value: 'browser', label: 'Встроенное в браузер' },
  { value: 'off', label: 'Выключено' },
];
async function select(value: string) {
  if (value === mode.value) return;
  if (value === 'browser' || isLocalNoiseEngine(value)) lab.engine.value = value;
  else if (value !== 'off') return;
  lab.suppression.value = value !== 'off';
  await props.devices.updateProcessing();
}
</script>
<template>
  <div class="audio-lab">
    <h3>Обработка микрофона</h3>
    <label for="noise-engine">Шумоподавление</label>
    <AppSelect
      id="noise-engine"
      label="Шумоподавление"
      :model-value="mode"
      :options="options"
      :disabled="disabled"
      @update:model-value="select"
    />
  </div>
</template>
