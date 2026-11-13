<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount } from 'vue';
const props = defineProps<{
  request: (path: string, method?: string, body?: unknown) => Promise<any>;
}>();
type State = {
  installed: string;
  available: string;
  phase: string;
  canInstall: boolean;
  checkedAt?: number;
  detail?: string;
};
const status = ref<State>();
const error = ref('');
const sending = ref(false);
const labels: Record<string, string> = {
  unmanaged: 'Обновление из панели пока не настроено на этом сервере.',
  checking: 'Проверяем релизы GitHub…',
  empty: 'Доступных обновлений нет.',
  current: 'Установлена актуальная версия.',
  available: 'Доступно обновление.',
  queued: 'Обновление в очереди.',
  waiting: 'Ждём окончания звонка.',
  installing: 'Устанавливаем обновление…',
  restored: 'Восстановлена предыдущая версия.',
  error: 'Не удалось завершить операцию.',
};
let timer: ReturnType<typeof setTimeout>;
let disposed = false;
let revision = 0;
function scheduleRefresh() {
  if (disposed) return;
  clearTimeout(timer);
  timer = setTimeout(refresh, status.value?.phase === 'checking' ? 350 : 5000);
}
async function refresh() {
  const started = revision;
  try {
    const result = await props.request('/api/server-update');
    if (started === revision && !sending.value) {
      status.value = result;
      error.value = '';
    }
  } catch (e) {
    if (started === revision) error.value = (e as Error).message;
  } finally {
    if (started === revision) scheduleRefresh();
  }
}
async function submit(action: 'check' | 'install') {
  if (!status.value || sending.value) return;
  sending.value = true;
  clearTimeout(timer);
  revision++;
  try {
    status.value =
      action === 'check'
        ? await props.request('/api/server-update/check', 'POST', {})
        : await props.request('/api/server-update', 'POST', { tag: status.value.available });
    error.value = '';
  } catch (e) {
    error.value = (e as Error).message;
  } finally {
    revision++;
    sending.value = false;
    scheduleRefresh();
  }
}
onMounted(refresh);
onBeforeUnmount(() => {
  disposed = true;
  clearTimeout(timer);
});
</script>
<template>
  <section class="editor-fill server-updates" aria-label="Обновления сервера">
    <h3>Обновления сервера</h3>
    <template v-if="status">
      <p>Установлено · {{ status.installed }}</p>
      <p v-if="status.available && status.available !== status.installed">
        Последний релиз · {{ status.available }}
      </p>
      <p class="field-help" role="status">
        {{ status.detail || labels[status.phase] }}
        <span v-if="status.checkedAt && status.phase !== 'checking'">
          · Проверено
          {{
            new Date(status.checkedAt * 1000).toLocaleString('ru-RU', {
              day: '2-digit',
              month: '2-digit',
              hour: '2-digit',
              minute: '2-digit',
            })
          }}</span
        >
      </p>
      <div v-if="status.phase !== 'unmanaged'" class="button-row">
        <button
          type="button"
          class="soft-button"
          :disabled="
            sending || ['checking', 'queued', 'waiting', 'installing'].includes(status.phase)
          "
          @click="submit('check')"
        >
          Проверить обновления
        </button>
        <button
          v-if="status.canInstall"
          type="button"
          class="soft-button"
          :disabled="sending"
          @click="submit('install')"
        >
          Обновить сервер
        </button>
      </div>
    </template>
    <p v-if="error" class="field-help" role="status">{{ error }}</p>
  </section>
</template>
<style scoped>
.server-updates {
  min-height: 0;
}
.server-updates > h3,
.server-updates > p {
  margin: 0 0 8px;
}
.server-updates > :last-child {
  margin-bottom: 0;
}
.server-updates .button-row {
  margin-top: 12px;
}
</style>
