<script setup lang="ts">
import SoftButton from './SoftButton.vue';
import { ref, onMounted, onBeforeUnmount } from 'vue';
const props = defineProps<{
  request: (path: string, method?: string, body?: unknown) => Promise<any>;
}>();
type Status = {
  enabled: boolean;
  scanLimitBytes?: number;
  version: string;
  updateConfigured: boolean;
  updater?: {
    busy: boolean;
    lastCheck: number;
    error: string;
  };
};
const data = ref<Status>(),
  busy = ref(false),
  error = ref(''),
  updating = ref(false);
let timer: ReturnType<typeof setInterval> | undefined;
let revision = 0;
async function load() {
  const started = ++revision;
  try {
    const status = await props.request('/api/antivirus');
    if (started === revision) data.value = status;
  } catch (e) {
    if (started === revision) error.value = (e as Error).message;
  }
}
async function save(event: Event) {
  if (busy.value || !data.value) return;
  const input = event.currentTarget as HTMLInputElement;
  revision++;
  busy.value = true;
  error.value = '';
  try {
    const result = await props.request('/api/antivirus', 'PATCH', { enabled: input.checked });
    data.value.enabled = !!result.enabled;
  } catch (e) {
    input.checked = data.value.enabled;
    error.value = (e as Error).message;
  } finally {
    busy.value = false;
  }
}
async function update() {
  if (busy.value) return;
  revision++;
  updating.value = busy.value = true;
  error.value = '';
  try {
    await props.request('/api/antivirus/update', 'POST');
    await load();
  } catch (e) {
    error.value = (e as Error).message;
  } finally {
    updating.value = busy.value = false;
  }
}
function date(ts: number) {
  return new Date(ts * 1000).toLocaleString('ru');
}
onMounted(() => {
  void load();
  timer = setInterval(() => {
    if (!busy.value) void load();
  }, 10000);
});
onBeforeUnmount(() => clearInterval(timer));
</script>
<template>
  <section class="antivirus-settings">
    <div class="antivirus-heading">
      <h3 id="antivirus-label">Антивирус для файлов в чате — ClamAV</h3>
      <input
        type="checkbox"
        role="switch"
        class="antivirus-switch"
        aria-labelledby="antivirus-label"
        :checked="data?.enabled"
        :disabled="!data || busy"
        @change="save"
      />
    </div>
    <p v-if="data?.enabled && data.scanLimitBytes" class="field-help">
      ClamAV проверяет файлы до {{ data.scanLimitBytes / 1024 ** 2 }} МБ включительно. Файлы крупнее
      публикуются без проверки.
    </p>
    <div v-if="data" class="antivirus-details">
      <div>
        <p>
          Версия ClamAV ·
          {{ data.version?.split('/')[0].replace(/^ClamAV\s+/i, '') || 'нет данных' }}
        </p>
        <p>
          Последняя проверка баз ·
          {{ data.updater?.lastCheck ? date(data.updater.lastCheck) : 'нет данных' }}
        </p>
      </div>
      <SoftButton :disabled="busy || data.updater?.busy || !data.updateConfigured" @click="update">
        {{ updating || data.updater?.busy ? 'Проверяем базы…' : 'Проверить обновления баз' }}
      </SoftButton>
    </div>
    <p v-if="error || data?.updater?.error" class="form-error" role="alert">
      {{ error || data?.updater?.error }}
    </p>
  </section>
</template>
<style scoped>
.antivirus-heading {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}
.antivirus-heading h3 {
  margin: 0;
}
.antivirus-details {
  margin-top: 12px;
}
.antivirus-details p {
  margin: 4px 0;
  color: #aaa;
  font-size: 12px;
}
.antivirus-details button {
  margin-top: 8px;
}
.antivirus-settings .antivirus-switch {
  appearance: none;
  box-sizing: border-box;
  flex: none;
  width: 34px;
  height: 20px;
  min-height: 0;
  margin: 0;
  padding: 3px;
  border: 0;
  border-radius: 10px;
  background: #474747;
  cursor: pointer;
}
.antivirus-switch::after {
  content: '';
  display: block;
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: #eee;
}
.antivirus-settings .antivirus-switch:checked {
  background: var(--settings-accent);
}
.antivirus-switch:checked::after {
  transform: translateX(14px);
}
.antivirus-switch:focus-visible {
  outline: 2px solid currentColor;
  outline-offset: 3px;
}
.antivirus-switch:disabled {
  opacity: 0.6;
}
</style>
