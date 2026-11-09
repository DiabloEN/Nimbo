<script setup lang="ts">
import AppSelect from './AppSelect.vue';
import AvatarEditor from './AvatarEditor.vue';
import RoleAssignment from './RoleAssignment.vue';
import { computed, nextTick, ref, watch } from 'vue';

import { permitted, editableMember, roleLabel } from '../composables/roles';
import ChannelEditor from './ChannelEditor.vue';
import type { ChannelSection, Channel, Member, Role } from '../composables/useWorkspace';
const props = defineProps<{
  sections: ChannelSection[];
  view: 'workspace' | 'channels';
  roles: Role[];
  avatar: string;
  sectionOrder: string[];
  avatars: Record<string, string>;
  name: string;
  description: string;
  channels: Channel[];
  members: Member[];
  self?: Member;
  ownerExists: boolean;
  mutate: (path: string, method: string, body?: unknown) => Promise<unknown>;
}>();
const draftAvatar = ref(props.avatar);
watch(
  () => props.avatar,
  (v, old) => {
    if (draftAvatar.value === old) draftAvatar.value = v;
  },
);
const description = ref(props.description);
watch(
  () => props.description,
  (value, old) => {
    if (description.value === old) description.value = value;
  },
);
const title = ref(props.name),
  busy = ref(false),
  error = ref(''),
  note = ref('');
watch(
  () => props.view,
  () => {
    note.value = '';
    error.value = '';
  },
);
watch(
  () => props.name,
  (value, old) => {
    if (title.value === old) title.value = value;
  },
);
let savingServer = false,
  saveAgain = false;
async function saveServer() {
  saveAgain = true;
  if (savingServer) return;
  savingServer = true;
  error.value = '';
  try {
    while (saveAgain) {
      saveAgain = false;
      const data = {
        name: title.value.trim(),
        description: description.value,
        avatar: draftAvatar.value,
      };
      if (!data.name) {
        error.value = 'Введите название сервера.';
        break;
      }
      if (
        data.name === props.name &&
        data.description === props.description &&
        data.avatar === props.avatar
      )
        continue;
      await props.mutate('/api/workspace', 'PATCH', data);
      note.value = 'Сохранено.';
    }
  } catch (e) {
    error.value = (e as Error).message;
  } finally {
    savingServer = false;
  }
}
function removeAvatar() {
  draftAvatar.value = '';
  void saveServer();
}
async function run(action: () => Promise<unknown>) {
  if (busy.value) return;
  busy.value = true;
  error.value = '';
  note.value = '';
  try {
    await action();
    note.value = 'Сохранено.';
  } catch (e) {
    error.value = (e as Error).message;
  } finally {
    busy.value = false;
  }
}
const can = (p: string) => permitted(props.self, p);

const memberQuery = ref(''),
  memberPage = ref(1),
  memberList = ref<HTMLElement>();
watch([memberQuery, memberPage], async () => {
  await nextTick();
  if (memberList.value) memberList.value.scrollTop = 0;
});
const filteredMembers = computed(() => {
  const query = memberQuery.value.trim().toLocaleLowerCase();
  return query
    ? props.members.filter((m) => m.name.toLocaleLowerCase().includes(query))
    : props.members;
});
const memberPages = computed(() => Math.max(1, Math.ceil(filteredMembers.value.length / 20)));
const visibleMembers = computed(() =>
  filteredMembers.value.slice((memberPage.value - 1) * 20, memberPage.value * 20),
);
watch(memberQuery, () => {
  memberPage.value = 1;
});
watch(memberPages, (total) => {
  memberPage.value = Math.min(memberPage.value, total);
});
const blockDuration = ref('0');
function blockLabel(m: Member) {
  return m.blockedUntil
    ? 'Заблокирован до ' +
        new Date(m.blockedUntil).toLocaleString('ru', {
          day: 'numeric',
          month: 'short',
          hour: '2-digit',
          minute: '2-digit',
        })
    : 'Доступ заблокирован';
}
const moderation = ref<{ member: Member; action: 'block' | 'unblock' }>();
</script>
<template>
  <section class="workspace-settings">
    <p v-if="error" class="form-error" role="alert">{{ error }}</p>
    <span class="sr-only" role="status">{{ note }}</span>
    <template v-if="view === 'channels'">
      <form v-if="can('manage_server')" class="space-name-form" @submit.prevent="saveServer">
        <div class="server-avatar-editor">
          <AvatarEditor
            :value="draftAvatar"
            :fallback="Array.from(title.trim())[0]?.toUpperCase() || 'N'"
            :disabled="busy"
            square
            @change="
              (value) => {
                draftAvatar = value;
                saveServer();
              }
            "
            @remove="removeAvatar"
          />
        </div>
        <label
          ><span class="sr-only">Название сервера</span
          ><input
            placeholder="Название сервера"
            v-model="title"
            required
            maxlength="60"
            @blur="saveServer" /></label
        ><label
          ><span class="sr-only">Подпись</span
          ><input placeholder="Подпись" v-model="description" maxlength="200" @blur="saveServer"
        /></label>
      </form>
      <ChannelEditor
        :roles="roles"
        :sections="sections"
        v-if="can('manage_channels')"
        :channels="channels"
        :section-order="sectionOrder"
        :mutate="mutate"
      />
    </template>
    <p
      v-if="view === 'channels' && !can('manage_channels') && !can('manage_server')"
      class="info-fill"
    >
      Каналами управляют владелец и назначенные им администраторы.
    </p>
    <template v-if="view === 'workspace'">
      <div class="member-search">
        <label class="sr-only" for="member-search">Поиск участников</label
        ><input
          id="member-search"
          v-model="memberQuery"
          type="search"
          placeholder="Найти участника по имени"
          autocomplete="off"
        /><span class="member-search-count" role="status"
          >{{ memberQuery.trim() ? 'Найдено' : 'Всего' }}: {{ filteredMembers.length }}</span
        >
      </div>
      <ul ref="memberList" class="managed-list member-results">
        <li v-for="m in visibleMembers" :key="m.id">
          <span class="avatar small"
            ><img v-if="avatars[m.id]" :src="avatars[m.id]" alt="" /><template v-else>{{
              m.name.slice(0, 2).toUpperCase()
            }}</template></span
          >
          <div>
            <strong>{{ m.name }}{{ m.id === self?.id ? ' · вы' : '' }}</strong
            ><small>{{ m.blocked ? blockLabel(m) : roleLabel(m, roles) }}</small>
          </div>
          <div v-if="editableMember(self, m)" class="member-actions">
            <RoleAssignment
              v-if="can('manage_roles') && !m.blocked"
              :member="m"
              :self="self"
              :roles="roles"
              :mutate="mutate"
            /><button
              v-if="can('ban_members')"
              class="soft-button"
              :disabled="busy"
              @click="
                blockDuration = '0';
                moderation = { member: m, action: m.blocked ? 'unblock' : 'block' };
              "
            >
              {{ m.blocked ? 'Разблокировать' : 'Заблокировать' }}
            </button>
          </div>
          <div v-if="moderation?.member.id === m.id" class="member-moderation" role="alert">
            <h3>
              {{ moderation.action === 'block' ? 'Заблокировать доступ' : 'Разблокировать' }}:
              {{ moderation.member.name }}?
            </h3>
            <label v-if="moderation.action === 'block'" class="block-duration"
              >Срок блокировки<AppSelect
                :id="'block-duration-' + m.id"
                label="Срок блокировки"
                v-model="blockDuration"
                :disabled="busy"
                :options="[
                  { value: '0', label: 'Навсегда' },
                  { value: '900', label: '15 минут' },
                  { value: '3600', label: '1 час' },
                  { value: '86400', label: '1 день' },
                  { value: '604800', label: '1 неделя' },
                  { value: '2592000', label: '30 дней' },
                ]"
            /></label>
            <div class="button-row">
              <button
                class="danger-button"
                :disabled="busy"
                @click="
                  run(async () => {
                    await mutate(`/api/members/${moderation!.member.id}/moderate`, 'POST', {
                      action: moderation!.action,
                      seconds: Number(blockDuration),
                    });
                    moderation = undefined;
                  })
                "
              >
                Подтвердить</button
              ><button class="text-button" :disabled="busy" @click="moderation = undefined">
                Отмена
              </button>
            </div>
          </div>
        </li>
      </ul>

      <p v-if="!filteredMembers.length" class="field-help member-empty">
        {{
          memberQuery.trim()
            ? 'Никого не нашли. Попробуйте другое имя.'
            : 'На сервере пока нет участников.'
        }}
      </p>
      <nav v-if="memberPages > 1" class="member-pagination" aria-label="Страницы участников">
        <span
          >{{ (memberPage - 1) * 20 + 1 }}–{{
            Math.min(memberPage * 20, filteredMembers.length)
          }}
          из {{ filteredMembers.length }}</span
        ><button
          type="button"
          class="soft-button"
          :disabled="memberPage === 1"
          @click="memberPage--"
        >
          Назад</button
        ><span>{{ memberPage }} / {{ memberPages }}</span
        ><button
          type="button"
          class="soft-button"
          :disabled="memberPage === memberPages"
          @click="memberPage++"
        >
          Далее
        </button>
      </nav>
    </template>
  </section>
</template>

<style scoped>
.member-search {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 16px;
}
.member-search input {
  flex: 1;
  min-width: 0;
  margin: 0;
}
.member-search-count {
  flex: none;
  font-size: 11px;
  color: var(--muted);
  font-variant-numeric: tabular-nums;
}
.member-results {
  max-height: 640px;
  max-height: min(640px, 65dvh);
  overflow-y: auto;
  overscroll-behavior: contain;
  scrollbar-width: thin;
  scrollbar-color: #484848 transparent;
}
.member-results li > div:not(.member-actions) {
  min-width: 0;
}
.member-results strong {
  overflow-wrap: anywhere;
}
.member-pagination {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 12px;
  margin-top: 16px;
  font-size: 11px;
  color: var(--muted);
  font-variant-numeric: tabular-nums;
}
.member-pagination > span:first-child {
  margin-right: auto;
}
.member-pagination button {
  font-size: 11px;
  padding: 8px 12px;
}
.member-pagination button:disabled {
  opacity: 0.35;
  cursor: default;
}
.member-empty {
  padding: 16px 0;
}
@media (max-width: 500px) {
  .member-search {
    flex-wrap: wrap;
  }
  .member-search input {
    flex-basis: 100%;
  }
  .member-pagination {
    gap: 8px;
    flex-wrap: wrap;
  }
}
</style>

<style scoped>
.member-results li {
  flex-wrap: wrap;
}
.member-results li > .member-moderation {
  flex-basis: 100%;
  padding-top: 14px;
  border-top: 1px solid #292929;
}
.member-moderation h3 {
  font-size: 12px;
}
.block-duration {
  max-width: 220px;
  margin: 12px 0;
}
.block-duration :deep(.app-select) {
  margin-top: 8px;
}
.member-moderation .button-row button {
  font-size: 11px;
  padding: 9px 12px;
}
</style>
