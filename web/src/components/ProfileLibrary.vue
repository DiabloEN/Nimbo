<script setup lang="ts">
import { ref } from 'vue';
import PasswordInput from './PasswordInput.vue';
import AppIcon from './AppIcon.vue';
import { parseProfile, type ProfileFile } from '../composables/profileFile';
import { decryptProfile, inspectEncryptedProfile } from '../composables/encryptedProfile';
import type { LocalProfile } from '../composables/profileLibrary';

const props = defineProps<{
  profiles: LocalProfile[];
  activeId: string;
  exportFile: (password: string) => Promise<void>;
  addProfile: (profile: ProfileFile) => string;
  switchProfile: (id: string, code: string) => Promise<void>;
  removeProfile: (id: string) => Promise<void>;
}>();

const exporting = ref(false);
const exportOpen = ref(false);
const password = ref('');
const repeat = ref('');
const note = ref('');
const encrypted = ref('');
const importPassword = ref('');
const busy = ref(false);
const switching = ref('');
const code = ref('');
const removing = ref('');

function reset() {
  note.value = '';
  encrypted.value = '';
  importPassword.value = '';
  switching.value = '';
  code.value = '';
  removing.value = '';
  exportOpen.value = false;
  password.value = '';
  repeat.value = '';
}

function add(profile: ProfileFile) {
  const before = props.profiles.map((p) => p.id);
  const id = props.addProfile(profile);
  encrypted.value = '';
  importPassword.value = '';
  note.value = before.includes(id)
    ? 'Профиль обновлён. Новые серверы добавлены, существующие доступы сохранены.'
    : 'Профиль загружен. Можно переключиться на него ниже.';
}

async function load(event: Event) {
  const input = event.target as HTMLInputElement,
    file = input.files?.[0];
  input.value = '';
  if (!file || busy.value) return;
  reset();
  busy.value = true;
  try {
    if (file.size > 300000) throw new Error('Файл слишком большой.');
    const text = await file.text();
    if (JSON.parse(text)?.format === 'nimbo-encrypted-profile') {
      inspectEncryptedProfile(text);
      encrypted.value = text;
    } else add(parseProfile(text));
  } catch (e) {
    note.value = (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function unlock() {
  busy.value = true;
  note.value = '';
  try {
    add(await decryptProfile(encrypted.value, importPassword.value));
  } catch (e) {
    note.value = (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function download() {
  if (password.value !== repeat.value) {
    note.value = 'Пароли не совпадают.';
    return;
  }
  exporting.value = true;
  note.value = '';
  try {
    await props.exportFile(password.value);
    reset();
    note.value = 'Защищённый файл .nimbo скачан.';
  } catch (e) {
    note.value = (e as Error).message;
  } finally {
    exporting.value = false;
  }
}

async function beginSwitch(id: string) {
  reset();
  switching.value = id;
  await activate();
}

async function activate() {
  busy.value = true;
  note.value = '';
  try {
    await props.switchProfile(switching.value, code.value);
    reset();
  } catch (e) {
    note.value = (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function remove() {
  busy.value = true;
  note.value = '';
  try {
    await props.removeProfile(removing.value);
    reset();
  } catch (e) {
    note.value = (e as Error).message;
  } finally {
    busy.value = false;
  }
}
</script>
<template>
  <section class="editor-fill profile-library" aria-labelledby="library-title">
    <h3 id="library-title">Профили</h3>
    <p class="field-help">
      Загрузите профиль .nimbo или скачайте текущий с именем, аватаром, горячими клавишами и списком
      серверов. Файл защищён паролем.
    </p>
    <p class="profile-local-note">
      Это ваш локальный профиль, облачного аккаунта нет. Файл .nimbo позволяет перенести его на
      другое устройство и войти на совместимые серверы Nimbo. Храните резервную копию и пароль к
      ней: если удалить данные браузера без копии файла, список серверов и сохранённые доступы не
      восстановить.
    </p>
    <div class="library-actions">
      <button
        type="button"
        class="soft-button"
        :disabled="busy || exporting"
        @click="
          reset();
          exportOpen = true;
        "
      >
        Скачать профиль</button
      ><label class="file-button" :class="{ disabled: busy || exporting }"
        >Загрузить профиль<input
          type="file"
          accept=".nimbo,.json,application/json,application/octet-stream"
          :disabled="busy || exporting"
          @change="load"
      /></label>
    </div>
    <form v-if="exportOpen" class="library-form" @submit.prevent="download">
      <div class="library-fields">
        <label
          >Пароль файла<PasswordInput
            v-model="password"
            required
            minlength="6"
            maxlength="256"
            autocomplete="new-password"
            :disabled="exporting" /></label
        ><label
          >Повторите пароль<PasswordInput
            v-model="repeat"
            required
            minlength="6"
            maxlength="256"
            autocomplete="new-password"
            :disabled="exporting"
        /></label>
      </div>
      <p class="field-help">
        От 6 символов. Пароль не отправляется на сервер; восстановить его нельзя.
      </p>
      <div class="button-row">
        <button class="primary" :disabled="exporting">
          {{ exporting ? 'Шифруем…' : 'Скачать .nimbo' }}</button
        ><button type="button" class="text-button" :disabled="exporting" @click="reset">
          Отмена
        </button>
      </div>
    </form>
    <form v-if="encrypted" class="library-form" @submit.prevent="unlock">
      <label
        >Пароль загружаемого файла<PasswordInput
          v-model="importPassword"
          required
          minlength="6"
          maxlength="256"
          autocomplete="off"
          :disabled="busy"
      /></label>
      <div class="button-row">
        <button class="primary" :disabled="busy">{{ busy ? 'Открываем…' : 'Загрузить' }}</button
        ><button type="button" class="text-button" :disabled="busy" @click="reset">Отмена</button>
      </div>
    </form>
    <div class="saved-profiles">
      <h4>
        В этом браузере <span>{{ profiles.length }}</span>
      </h4>
      <ul>
        <li v-for="entry in profiles" :key="entry.id">
          <div class="local-profile-row">
            <span class="local-profile-avatar"
              ><img v-if="entry.profile.avatar" :src="entry.profile.avatar" alt="" /><template
                v-else
                >{{ entry.profile.name.slice(0, 2).toUpperCase() }}</template
              ></span
            >
            <div class="local-profile-name">
              <strong>{{ entry.profile.name }}</strong
              ><small>{{
                entry.id === activeId
                  ? 'Текущий профиль'
                  : 'Серверов: ' + entry.profile.servers.length
              }}</small>
            </div>
            <button
              v-if="entry.id !== activeId"
              type="button"
              class="soft-button switch-profile"
              :disabled="busy || exporting"
              :aria-label="'Переключиться на ' + entry.profile.name"
              @click="beginSwitch(entry.id)"
            >
              Войти</button
            ><button
              type="button"
              class="remove-profile"
              :disabled="busy || exporting"
              :aria-label="'Удалить профиль ' + entry.profile.name"
              title="Удалить из браузера"
              @click="
                reset();
                removing = entry.id;
              "
            >
              <AppIcon name="close" :size="16" />
            </button>
          </div>
          <form
            v-if="switching === entry.id && !busy"
            class="library-form"
            @submit.prevent="activate"
          >
            <label
              >Код приглашения для первого входа<input
                v-model="code"
                type="password"
                required
                maxlength="256"
                autocomplete="off"
                :disabled="busy"
            /></label>
            <p class="field-help">
              При переключении текущий звонок завершится. Если профиль ещё не был на этом сервере,
              он присоединится как новый участник.
            </p>
            <div class="button-row">
              <button class="primary" :disabled="busy">
                {{ busy ? 'Входим…' : 'Переключиться' }}</button
              ><button type="button" class="text-button" :disabled="busy" @click="reset">
                Отмена
              </button>
            </div>
          </form>
          <div v-if="removing === entry.id" class="library-form">
            <p class="field-help">
              Удалить профиль из этого браузера? Скачанные файлы и профиль на сервере останутся.{{
                entry.id === activeId ? ' Вы выйдете из текущего профиля.' : ''
              }}
            </p>
            <div class="button-row">
              <button type="button" class="danger-button" :disabled="busy" @click="remove">
                Удалить профиль</button
              ><button type="button" class="text-button" :disabled="busy" @click="reset">
                Отмена
              </button>
            </div>
          </div>
        </li>
      </ul>
    </div>
    <p v-if="note" class="field-help" role="status">{{ note }}</p>
  </section>
</template>
<style scoped>
.profile-local-note {
  font-size: 11px;
  line-height: 1.6;
  color: #929292;
  margin: 12px 0 20px;
  max-width: 62em;
}
.profile-library {
  margin: 32px 0 12px;
}
.library-actions {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}
.library-actions > button,
.library-actions > .file-button {
  font-size: 12px;
  margin: 0;
  min-height: 38px;
  display: inline-flex;
  align-items: center;
  padding: 10px 14px;
  border-radius: 10px;
  background: #252525;
  color: #ddd;
  cursor: pointer;
}
.library-actions > button:hover,
.library-actions > .file-button:hover {
  background: #303030;
}
.file-button.disabled {
  opacity: 0.5;
  pointer-events: none;
}
.library-form {
  margin-top: 16px;
}
.library-fields {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}
.library-form label {
  font-size: 12px;
  min-width: 0;
}
.library-form input {
  width: 100%;
  margin-top: 7px;
}
.library-form .button-row {
  flex-wrap: wrap;
  margin-top: 12px;
}
.library-form button {
  font-size: 12px;
}
.saved-profiles {
  margin-top: 24px;
}
.saved-profiles h4 {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 11px;
  color: #999;
  font-weight: 400;
  margin: 0 0 10px;
}
.saved-profiles h4 span {
  color: #666;
}
.saved-profiles ul {
  list-style: none;
  padding: 0;
  margin: 0;
  display: grid;
  gap: 8px;
  max-height: 420px;
  overflow: auto;
  scrollbar-width: thin;
}
.saved-profiles li {
  background: #151515;
  border-radius: 12px;
  padding: 12px;
}
.local-profile-row {
  display: flex;
  align-items: center;
  gap: 10px;
}
.local-profile-avatar {
  width: 34px;
  height: 34px;
  flex: none;
  border-radius: 50%;
  display: grid;
  place-items: center;
  background: #333;
  font-size: 11px;
  overflow: hidden;
}
.local-profile-avatar img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.local-profile-name {
  flex: 1;
  min-width: 0;
}
.local-profile-name strong {
  font-size: 12px;
  display: block;
  overflow-wrap: anywhere;
}
.local-profile-name small {
  display: block;
  font-size: 10px;
  color: #888;
  margin-top: 4px;
}
.switch-profile {
  border-radius: 8px;
  font-size: 11px;
  padding: 8px 10px;
}
.remove-profile {
  display: grid;
  place-items: center;
  width: 28px;
  height: 28px;
  flex: none;
  padding: 0;
  background: transparent;
  color: #888;
  border-radius: 8px;
}
.remove-profile:hover {
  background: #333;
  color: #ef9292;
}
@media (max-width: 650px) {
  .library-fields {
    grid-template-columns: 1fr;
  }
  .profile-library {
    margin-top: 28px;
  }
}
</style>
