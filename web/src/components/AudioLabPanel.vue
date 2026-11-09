<script setup lang="ts">
import {computed} from 'vue';
import AppSelect from './AppSelect.vue';
import {isLocalNoiseEngine} from '../composables/audioLab';
import type {useAudioDevices} from '../composables/useAudioDevices';

const props = defineProps<{devices: ReturnType<typeof useAudioDevices>; disabled: boolean}>();
const lab = props.devices.lab;
const mode = computed(() => lab.suppression.value ? lab.engine.value : 'off');
const options = [
  {value: 'off', label: 'Выключено'},
  {value: 'browser', label: 'Встроенное в браузер'},
  {value: 'rnnoise', label: 'RNNoise'},
  {value: 'speex', label: 'Speex'},
  {value: 'gtcrn', label: 'GTCRN'},
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
  <fieldset class="audio-lab">
    <legend>Обработка микрофона · эксперимент</legend>
    <label for="noise-engine">Шумоподавление</label>
    <AppSelect id="noise-engine" label="Шумоподавление" :model-value="mode" :options="options" :disabled="disabled" @update:model-value="select"/>
    <p class="field-help">Сравните режимы через «Проверить микрофон». Обработка выполняется на вашем устройстве.</p>
    <label><input v-model="lab.gain.value" type="checkbox" :disabled="disabled" @change="devices.updateProcessing"> Автоматически выравнивать громкость микрофона</label>
    <p class="field-help">Выравнивание может усиливать тихие шорохи. Эхоподавление остаётся включённым.</p>
  </fieldset>
</template>
