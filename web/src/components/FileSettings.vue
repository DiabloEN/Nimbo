<script setup lang="ts">
import { ref, watch } from 'vue';
import type { Channel } from '../composables/useWorkspace';
import AppSelect from './AppSelect.vue';
const props = defineProps<{
  channels: Channel[];
  request: (path: string, method?: string, body?: unknown) => Promise<any>;
}>();
const channel = ref(''),
  enabled = ref(false),
  scan = ref(true),
  configured = ref(false),
  busy = ref(false),
  error = ref('');
let generation = 0;
watch(
  () => props.channels,
  () => {
    if (!props.channels.some((c) => c.id === channel.value && c.kind === 'text'))
      channel.value = props.channels.find((c) => c.kind === 'text')?.id || '';
  },
  { immediate: true },
);
watch(
  channel,
  async (id) => {
    const version = ++generation;
    error.value = '';
    if (!id) return;
    busy.value = true;
    try {
      const r = await props.request('/api/channels/' + encodeURIComponent(id) + '/files');
      if (version === generation) {
        enabled.value = r.policy.enabled;
        scan.value = r.policy.scan;
        configured.value = r.scannerConfigured;
      }
    } catch (e) {
      if (version === generation) error.value = (e as Error).message;
    } finally {
      if (version === generation) busy.value = false;
    }
  },
  { immediate: true },
);
async function save() {
  busy.value = true;
  error.value = '';
  try {
    await props.request('/api/channels/' + encodeURIComponent(channel.value) + '/files', 'PATCH', {
      enabled: enabled.value,
      scan: scan.value,
    });
  } catch (e) {
    error.value = (e as Error).message;
  } finally {
    busy.value = false;
  }
}
</script>
<template>
  <section v-if="channels.some((c) => c.kind === 'text')" class="file-settings">
    <h3>Файлы в текстовых каналах</h3>
    <AppSelect
      id="file-policy-channel"
      label="Текстовый канал"
      v-model="channel"
      :disabled="busy"
      :options="
        channels.filter((c) => c.kind === 'text').map((c) => ({ value: c.id, label: c.name }))
      "
    />
    <form @submit.prevent="save">
      <label
        ><input v-model="enabled" type="checkbox" :disabled="busy" /> Разрешить файлы до 20
        МБ</label
      >
      <p class="field-help">
        Антивирус: {{ scan ? 'проверка включена' : 'проверка выключена' }}.
        <a href="#/settings/general">Настроить в общих настройках</a>
      </p>
      <p v-if="scan && !configured" class="field-help">
        ClamAV не настроен на хосте. При включённой проверке загрузка будет недоступна.
      </p>
      <p v-else-if="!scan" class="field-help">Файлы будут доступны без антивирусной проверки.</p>
      <p v-else class="field-help">
        Файл публикуется только после ответа антивируса. Проверка не гарантирует отсутствие всех
        угроз.
      </p>
      <button class="soft-button" :disabled="busy">{{ busy ? 'Сохраняем…' : 'Сохранить' }}</button>
      <p v-if="error" class="form-error" role="alert">{{ error }}</p>
    </form>
  </section>
</template>
<style scoped>
.settings-content .file-settings {
  min-height: 0;
  margin-top: 24px;
  padding: 18px;
  background: #171717;
  border-radius: 14px;
}
.file-settings h3 {
  font-size: 13px;
  margin-bottom: 16px;
}
.file-settings label {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
}
.file-settings input {
  width: auto;
  margin: 0;
}
</style>
