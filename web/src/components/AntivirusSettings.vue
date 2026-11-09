<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount } from 'vue';
const props = defineProps<{
  request: (path: string, method?: string, body?: unknown) => Promise<any>;
}>();
type Status = {
  enabled: boolean;
  name: string;
  configured: boolean;
  available: boolean;
  version: string;
  updateConfigured: boolean;
  updater?: {
    busy: boolean;
    lastCheck: number;
    lastSuccess: number;
    error: string;
    memoryBytes?: number;
    serviceMemoryBytes?: number;
    memoryLimitBytes?: number;
    cpuLimitPercentOfHost?: number;
  };
};
const data = ref<Status>(),
  enabled = ref(true),
  busy = ref(false),
  error = ref(''),
  note = ref('');
let timer: ReturnType<typeof setInterval> | undefined;
async function load(initial = false) {
  try {
    data.value = await props.request('/api/antivirus');
    if (initial) enabled.value = !!data.value?.enabled;
  } catch (e) {
    error.value = (e as Error).message;
  }
}
async function save() {
  if (busy.value || !data.value) return;
  const previous = data.value.enabled;
  busy.value = true;
  error.value = '';
  note.value = '';
  try {
    const result = await props.request('/api/antivirus', 'PATCH', { enabled: enabled.value });
    enabled.value = !!result.enabled;
    data.value.enabled = enabled.value;
    note.value = 'Изменение сохранено';
  } catch (e) {
    enabled.value = previous;
    error.value = (e as Error).message;
  } finally {
    busy.value = false;
  }
}
async function update() {
  busy.value = true;
  error.value = '';
  note.value = '';
  try {
    await props.request('/api/antivirus/update', 'POST');
    note.value =
      'Проверяем обновления баз. Новые базы установятся автоматически. На время обновления проверка файлов приостановится.';
    await load();
  } catch (e) {
    error.value = (e as Error).message;
  } finally {
    busy.value = false;
  }
}
function date(ts: number) {
  return new Date(ts * 1000).toLocaleString('ru');
}
onMounted(() => {
  void load(true);
  timer = setInterval(() => void load(), 10000);
});
onBeforeUnmount(() => clearInterval(timer));
</script>
<template>
  <section class="antivirus-settings">
    <h3>Антивирус файлов</h3>
    <p class="field-help">
      ClamAV проверяет вложения на этом сервере. Содержимое файлов не передаётся внешним сервисам.
    </p>
    <template v-if="data"
      ><div class="antivirus-status">
        <strong
          >ClamAV ·
          {{
            data.available ? 'готов к проверке' : data.configured ? 'недоступен' : 'не установлен'
          }}</strong
        >
        <p v-if="data.version">{{ data.version }}</p>
        <p v-if="data.updater?.lastSuccess">Базы проверены: {{ date(data.updater.lastSuccess) }}</p>
        <p v-if="data.updater?.memoryBytes">
          Память сканера сейчас: {{ Math.round(data.updater.memoryBytes / 1024 / 1024) }} МБ
        </p>
        <p v-if="data.updater?.memoryLimitBytes">
          Лимиты службы на этом сервере:
          {{ Math.round(data.updater.memoryLimitBytes / 1024 / 1024) }} МБ RAM, до
          {{ data.updater.cpuLimitPercentOfHost }}% всего CPU. Это потолок, а не постоянное
          потребление.
        </p>
      </div>
      <div class="antivirus-policy">
        <label
          ><input v-model="enabled" type="checkbox" :disabled="busy" @change="save" /> Проверять
          файлы антивирусом во всех каналах</label
        >
        <p class="field-help">
          {{
            enabled
              ? 'Файл появится в чате после успешной проверки. Если сканер недоступен, файл не отправится.'
              : 'Новые файлы будут отправляться без антивирусной проверки.'
          }}
        </p>
      </div>
      <p v-if="!data.updateConfigured" class="field-help">
        Обновление баз через интерфейс ещё не настроено администратором хоста.
      </p>
      <p v-if="data.updater?.error" class="form-error">{{ data.updater.error }}</p></template
    >
    <p class="field-help">
      Ориентир для сервера: 1–2 ГБ памяти для загруженных баз, больше во время обновления. Процессор
      нагружается при проверке; архивы обычно требуют больше ресурсов. Для ClamAV рекомендуют
      минимум 3 ГиБ RAM. Выключение проверки здесь не останавливает службу и не освобождает её
      память.
    </p>
    <p class="field-help">
      Версия движка и номер базы указаны выше. Движок обновляет администратор хоста. Антивирус не
      гарантирует отсутствие всех угроз.
    </p>
    <p v-if="error" class="form-error" role="alert">{{ error }}</p>
    <p v-if="note" class="field-help" role="status">{{ note }}</p>
    <div v-if="data" class="antivirus-actions">
      <button
        class="text-button"
        :disabled="busy || data.updater?.busy || !data.updateConfigured"
        @click="update"
      >
        {{ data.updater?.busy ? 'Обновляем базы…' : 'Проверить обновления баз' }}
      </button>
    </div>
  </section>
</template>
<style scoped>
.settings-content .antivirus-settings {
  min-height: 0;
  padding: 20px;
  margin-top: 24px;
  background: #171717;
  border-radius: 14px;
}
.antivirus-settings h3 {
  font-size: 14px;
}
.antivirus-status {
  padding: 12px 0;
  font-size: 12px;
  overflow-wrap: anywhere;
}
.antivirus-status p {
  color: #aaa;
  margin-top: 6px;
}
.antivirus-settings label {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
}
.antivirus-settings input {
  width: auto;
  margin: 0;
}
.antivirus-actions {
  display: flex;
  justify-content: flex-end;
  margin-top: 16px;
}
.antivirus-actions .text-button {
  font-size: 12px;
  text-align: right;
}
</style>
