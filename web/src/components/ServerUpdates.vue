<script setup lang="ts">
import SoftButton from './SoftButton.vue';
import { ref, onMounted, onBeforeUnmount } from 'vue';
const props = defineProps<{
  request: (path: string, method?: string, body?: unknown) => Promise<any>;
}>();
type State = {
  installed: string;
  available: string;
  previous?: string;
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
async function submit(action: 'check' | 'install' | 'rollback') {
  if (!status.value || sending.value) return;
  sending.value = true;
  clearTimeout(timer);
  revision++;
  try {
    const path = action === 'install' ? '/api/server-update' : '/api/server-update/' + action;
    const tag = action === 'rollback' ? status.value.previous : status.value.available;
    status.value = await props.request(path, 'POST', action === 'check' ? {} : { tag });
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
  <section class="server-updates" aria-labelledby="server-updates-heading">
    <h3 id="server-updates-heading">Обновление</h3>
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
        <SoftButton
          :disabled="
            sending || ['checking', 'queued', 'waiting', 'installing'].includes(status.phase)
          "
          @click="submit('check')"
        >
          Проверить обновления
        </SoftButton>
        <SoftButton v-if="status.canInstall" :disabled="sending" @click="submit('install')">
          Обновить сервер и веб-клиент
        </SoftButton>
        <SoftButton
          v-if="status.previous"
          :disabled="
            sending || ['checking', 'queued', 'waiting', 'installing'].includes(status.phase)
          "
          @click="submit('rollback')"
        >
          Откатиться на {{ status.previous }}
        </SoftButton>
      </div>
    </template>
    <p v-if="error" class="field-help" role="status">{{ error }}</p>
  </section>
</template>
<style scoped>
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
