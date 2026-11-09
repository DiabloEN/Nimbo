<script setup lang="ts">
import { activeServer } from '../composables/serverConnection';
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import AppSelect from './AppSelect.vue';
import type { Invitation } from '../composables/useWorkspace';
const props = defineProps<{
  invitations: Invitation[];
  request: (path: string, method: string, body?: unknown) => Promise<unknown>;
}>();
const creating = ref(false),
  duration = ref('personal'),
  busy = ref(false),
  error = ref(''),
  copied = ref(''),
  now = ref(Date.now());
const active = computed(() =>
  props.invitations
    .filter((i) => !i.expiresAt || i.expiresAt > now.value)
    .slice()
    .sort((a, b) => b.createdAt - a.createdAt),
);
let timer: ReturnType<typeof setInterval> | undefined;
onMounted(() => {
  timer = setInterval(() => (now.value = Date.now()), 1000);
});
onBeforeUnmount(() => clearInterval(timer));
function link(item: Invitation) {
  return activeServer.value + '/#/invite/' + encodeURIComponent(item.code);
}
function expiry(item: Invitation) {
  return item.expiresAt
    ? 'До ' +
        new Date(item.expiresAt).toLocaleString('ru', {
          day: 'numeric',
          month: 'short',
          hour: '2-digit',
          minute: '2-digit',
        })
    : 'Без срока';
}
async function run(action: () => Promise<unknown>) {
  if (busy.value) return;
  busy.value = true;
  error.value = '';
  try {
    await action();
    creating.value = false;
  } catch (e) {
    error.value = (e as Error).message;
  } finally {
    busy.value = false;
  }
}
function shareText(item: Invitation) {
  return (
    'Приглашение в Nimbo\nСсылка для входа: ' +
    link(item) +
    '\nАдрес сервера: ' +
    activeServer.value +
    '\nКод приглашения: ' +
    item.code
  );
}
async function copy(item: Invitation) {
  try {
    await navigator.clipboard.writeText(shareText(item));
    copied.value = item.id;
  } catch {
    error.value = 'Не удалось скопировать. Выделите приглашение вручную.';
  }
}
</script>
<template>
  <section class="invitation-settings">
    <p v-if="error" class="form-error" role="alert">{{ error }}</p>
    <ul v-if="active.length" class="invitation-list">
      <li v-for="item in active" :key="item.id">
        <div class="invitation-meta">
          <strong>{{ item.singleUse ? 'Персональное' : 'Приглашение' }}</strong
          ><small>{{ expiry(item) }}</small
          ><button
            type="button"
            class="text-button"
            :disabled="busy"
            :aria-label="'Отозвать приглашение ' + item.id"
            @click="run(() => request('/api/invitations/' + item.id, 'DELETE'))"
          >
            Отозвать
          </button>
        </div>
        <textarea
          readonly
          :value="shareText(item)"
          rows="4"
          aria-label="Текст приглашения"
          @focus="($event.target as HTMLTextAreaElement).select()"
        ></textarea>
        <div class="invitation-actions">
          <button type="button" class="soft-button" @click="copy(item)">
            {{ copied === item.id ? 'Скопировано' : 'Скопировать приглашение' }}
          </button>
        </div>
      </li>
    </ul>
    <p v-else class="field-help">Активных приглашений нет.</p>
    <form
      v-if="creating"
      class="invitation-create"
      @submit.prevent="
        run(() =>
          request('/api/invitations', 'POST', {
            singleUse: duration === 'personal',
            seconds: duration === 'personal' ? 0 : Number(duration),
          }),
        )
      "
    >
      <div class="invitation-options">
        <label
          >Приглашение<AppSelect
            id="invite-duration"
            label="Тип приглашения"
            v-model="duration"
            :disabled="busy"
            :options="[
              { value: 'personal', label: 'Персональное' },
              { value: '3600', label: '1 час' },
              { value: '86400', label: '1 день' },
              { value: '604800', label: '1 неделя' },
              { value: '2592000', label: '30 дней' },
              { value: '0', label: 'Бессрочное' },
            ]"
        /></label>
      </div>
      <div class="invitation-actions">
        <button class="primary" :disabled="busy">Создать приглашение</button
        ><button type="button" class="text-button" :disabled="busy" @click="creating = false">
          Отмена
        </button>
      </div>
    </form>
    <button
      v-else
      type="button"
      class="soft-button create-invitation"
      :disabled="busy"
      @click="
        creating = true;
        error = '';
      "
    >
      Создать приглашение
    </button>
  </section>
</template>
<style scoped>
.invitation-list {
  list-style: none;
  padding: 0;
  margin: 0;
  display: flex;
  flex-direction: column;
  gap: 8px;
  max-height: 400px;
  overflow: auto;
  scrollbar-width: thin;
}
.invitation-list li,
.invitation-create {
  padding: 14px 16px;
  background: var(--surface);
  border-radius: 12px;
}
.invitation-meta {
  display: flex;
  align-items: center;
  gap: 12px;
  font-size: 12px;
}
.invitation-meta small {
  font-size: 11px;
  color: var(--muted);
}
.invitation-meta button {
  margin-left: auto;
  font-size: 11px;
  padding: 6px 8px;
}
.invitation-list textarea {
  width: 100%;
  resize: none;
  line-height: 1.6;
  font-size: 11px;
  margin: 10px 0;
  padding: 10px 12px;
}
.invitation-actions {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}
.invitation-actions button {
  font-size: 11px;
  padding: 9px 12px;
}
.invitation-options {
  display: flex;
  gap: 12px;
  margin-bottom: 14px;
}
.invitation-options label {
  flex: 1;
  min-width: 0;
  margin: 0;
  font-size: 11px;
}
.invitation-options :deep(.app-select) {
  margin-top: 8px;
}
.invitation-create,
.create-invitation {
  margin-top: 16px;
}
@media (max-width: 500px) {
  .invitation-options {
    flex-direction: column;
  }
  .invitation-meta {
    flex-wrap: wrap;
  }
}
</style>
