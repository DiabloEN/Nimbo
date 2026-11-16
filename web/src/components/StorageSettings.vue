<script setup lang="ts">
import SoftButton from './SoftButton.vue';
import { ref, computed, onMounted, onBeforeUnmount } from 'vue';
const props = defineProps<{
  request: (path: string, method: string, body?: unknown) => Promise<unknown>;
}>();
type Status = {
  policy: { limitBytes: number; maxFileBytes?: number; autoCleanup: boolean };
  legacyDisabledFiles?: boolean;
  usedBytes: number;
  breakdown?: Record<string, number>;
  messages: number;
  disk: { freeBytes: number; totalBytes: number } | null;
};
const data = ref<Status>(),
  limit = ref(0),
  fileLimit = ref(0),
  busy = ref(false),
  error = ref(''),
  note = ref('');
let timer: ReturnType<typeof setInterval> | undefined;
const gib = 1024 ** 3;
const categories = [
  ['messages', 'Сообщения'],
  ['photos', 'Фото'],
  ['video', 'Видео'],
  ['audio', 'Аудио'],
  ['files', 'Файлы'],
  ['other', 'Профили и настройки'],
];
const maxLimit = computed(() =>
  data.value?.disk ? Math.floor((data.value.disk.totalBytes / gib) * 1000) / 1000 : undefined,
);
const savedLimit = computed(() =>
  data.value ? Math.min(data.value.policy.limitBytes / gib, maxLimit.value ?? Infinity) : 0,
);
const mib = 1024 ** 2;
const quotaBytes = computed(() =>
  data.value && limit.value === savedLimit.value
    ? data.value.policy.limitBytes
    : Math.round(limit.value * gib),
);
const fileMaximum = computed(() => Math.min(1536, Math.floor((quotaBytes.value * 0.8) / mib)));
const changed = computed(
  () =>
    !!data.value &&
    (quotaBytes.value !== data.value.policy.limitBytes ||
      (data.value.policy.maxFileBytes !== undefined &&
        fileLimit.value * mib !== data.value.policy.maxFileBytes) ||
      data.value.legacyDisabledFiles),
);
function size(n: number) {
  const unit = n >= 1024 ** 3 ? 3 : n >= 1024 ** 2 ? 2 : n >= 1024 ? 1 : 0;
  return (
    new Intl.NumberFormat('ru', { maximumFractionDigits: 1 }).format(n / 1024 ** unit) +
    ' ' +
    ['Б', 'КБ', 'МБ', 'ГБ'][unit]
  );
}
async function load(initial = false) {
  try {
    data.value = (await props.request('/api/storage', 'GET')) as Status;
    if (initial) {
      limit.value = savedLimit.value;
      fileLimit.value = (data.value.policy.maxFileBytes ?? 0) / mib;
    }
  } catch (e) {
    error.value = (e as Error).message;
  }
}
async function save() {
  if (busy.value || !changed.value) return;
  busy.value = true;
  error.value = '';
  note.value = '';
  try {
    data.value = (await props.request('/api/storage', 'PATCH', {
      limitBytes: quotaBytes.value,
      ...(data.value?.policy.maxFileBytes !== undefined && {
        maxFileBytes: Math.round(fileLimit.value * mib),
      }),
    })) as Status;
    note.value = 'Лимиты обновлены.';
  } catch (e) {
    error.value = (e as Error).message;
  } finally {
    busy.value = false;
  }
}
onMounted(() => {
  void load(true);
  timer = setInterval(() => void load(), 15000);
});
onBeforeUnmount(() => clearInterval(timer));
</script>
<template>
  <section class="storage-settings">
    <h3>Хранилище сервера</h3>
    <div v-if="data" class="storage-meter">
      <div>
        <strong>{{ size(data.usedBytes) }}</strong
        ><span>из {{ size(data.policy.limitBytes) }}</span>
      </div>
      <progress
        :value="data.usedBytes"
        :max="data.policy.limitBytes"
        aria-label="Занятое место"
      /><small>Сообщений: {{ data.messages }}</small>
      <dl v-if="data.breakdown" class="storage-breakdown">
        <div v-for="[key, label] in categories" :key="key">
          <dt>{{ label }}</dt>
          <dd>{{ size(data.breakdown[key!] || 0) }}</dd>
        </div>
      </dl>
      <p v-if="data.disk">
        Свободно на разделе хранилища: {{ size(data.disk.freeBytes) }} из
        {{ size(data.disk.totalBytes) }}
      </p>
      <p v-else>Показатели диска недоступны.</p>
    </div>
    <form @submit.prevent="save">
      <div class="storage-limit-row">
        <div class="storage-limit-card">
          <label for="storage-limit">Лимит хранения</label>
          <div class="storage-value">
            <input
              id="storage-limit"
              type="number"
              v-model.number="limit"
              :min="8 / 1024"
              :max="maxLimit"
              step="any"
              required
              :disabled="busy || !data"
              :aria-describedby="maxLimit ? 'storage-unit storage-limit-help' : 'storage-unit'"
              @input="note = ''"
            />
            <span id="storage-unit" class="storage-unit">GB</span>
          </div>
          <p v-if="maxLimit" id="storage-limit-help" class="field-help">
            Максимум — {{ maxLimit.toLocaleString('ru') }} ГБ.
          </p>
        </div>
        <div v-if="data?.policy.maxFileBytes !== undefined" class="storage-limit-card">
          <label for="file-limit">Макс. размер файла для загрузки в чат</label>
          <div class="storage-value">
            <input
              id="file-limit"
              type="number"
              v-model.number="fileLimit"
              :max="fileMaximum"
              min="1"
              step="1"
              required
              :disabled="busy"
              aria-describedby="file-unit file-limit-help"
              @input="note = ''"
            />
            <span id="file-unit" class="storage-unit">MB</span>
          </div>
          <p id="file-limit-help" class="field-help">
            До
            {{
              fileMaximum >= 1024 && fileMaximum % 512 === 0
                ? (fileMaximum / 1024).toLocaleString('ru') + ' ГБ'
                : fileMaximum.toLocaleString('ru') + ' МБ'
            }}, в пределах 80% лимита.
          </p>
        </div>
        <SoftButton type="submit" :disabled="busy || !changed">Обновить</SoftButton>
      </div>
      <p v-if="data?.legacyDisabledFiles" class="field-help">
        После обновления лимитов вложения станут доступны во всех текстовых каналах с правом
        отправки.
      </p>
      <p class="field-help">
        При заполнении 90% лимита удаляются самые старые сообщения в чатах. Удаление необратимо.
        Если уменьшить лимит хранения и нажать «Обновить», лишняя история удалится сразу. Профили,
        аватары и каналы сохраняются.
      </p>
    </form>
    <p v-if="error" class="form-error" role="alert">{{ error }}</p>
    <p v-if="note" class="field-help" role="status">{{ note }}</p>
  </section>
</template>
<style scoped>
.storage-breakdown {
  margin: 18px 0 0;
  font-size: 13px;
}
.storage-breakdown > div {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  padding: 7px 0;
  border-bottom: 1px solid #ffffff08;
}
.storage-breakdown dt {
  color: #aaa;
}
.storage-breakdown dd {
  margin: 0;
  font-variant-numeric: tabular-nums;
}
.storage-meter {
  padding: 20px;
  background: #171717;
  border-radius: 14px;
  margin: 20px 0;
}
.storage-meter > div {
  display: flex;
  gap: 8px;
  align-items: baseline;
}
.storage-meter strong {
  font-size: 20px;
}
.storage-meter span,
.storage-meter small,
.storage-meter p {
  font-size: 12px;
  color: #999;
}
.storage-meter progress {
  display: block;
  width: 100%;
  height: 8px;
  appearance: none;
  border: 0;
  margin: 16px 0 10px;
  border-radius: 4px;
  overflow: hidden;
}
.storage-meter progress::-webkit-progress-bar {
  background: #333;
}
.storage-meter progress::-webkit-progress-value {
  background: var(--settings-accent);
}
.storage-meter progress::-moz-progress-bar {
  background: var(--settings-accent);
}
.storage-meter p {
  margin-top: 14px;
}
.storage-settings form {
  container-type: inline-size;
}
.storage-limit-row {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px;
}
.storage-limit-card {
  background: #171717;
  border-radius: 14px;
  padding: 14px;
}
.storage-limit-card label {
  display: block;
  margin: 0 0 8px;
}
.storage-limit-card .field-help {
  margin: 8px 0 0;
}
.storage-limit-row button {
  grid-column: 1 / -1;
  justify-self: start;
}
.storage-limit-card:only-of-type {
  grid-column: 1 / -1;
}
@container (max-width: 380px) {
  .storage-limit-row {
    grid-template-columns: minmax(0, 1fr);
  }
}
.storage-value {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}
.storage-unit {
  color: #aaa;
  font-size: 12px;
}
.storage-limit-row input,
.storage-limit-row button {
  height: var(--compact-control-height);
}
.storage-limit-row input {
  width: 68px;
  margin: 0;
  padding: 0 8px;
}
</style>
