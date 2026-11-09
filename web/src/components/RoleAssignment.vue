<script setup lang="ts">
import { ref, watch, nextTick, onMounted, onBeforeUnmount } from 'vue';
import type { Member, Role } from '../composables/useWorkspace';
import { permitted } from '../composables/roles';
const props = defineProps<{
  member: Member;
  self?: Member;
  roles: Role[];
  mutate: (path: string, method: string, body?: unknown) => Promise<unknown>;
}>();
const open = ref(false),
  busy = ref(false),
  error = ref(''),
  selected = ref<string[]>([]),
  trigger = ref<HTMLButtonElement>(),
  panel = ref<HTMLElement>(),
  left = ref(0),
  top = ref(0);
const current = () => [
  ...(props.member.roleIds ?? (props.member.role === 'admin' ? ['admin'] : [])),
];
watch(
  () => props.member,
  () => {
    if (!busy.value) selected.value = current();
  },
  { immediate: true },
);
function close() {
  open.value = false;
}
async function toggle() {
  if (open.value) {
    close();
    return;
  }
  selected.value = current();
  error.value = '';
  const box = trigger.value!.getBoundingClientRect();
  left.value = Math.max(8, Math.min(box.right - 240, innerWidth - 248));
  top.value = box.bottom + 6;
  open.value = true;
  await nextTick();
  const height = panel.value!.offsetHeight;
  top.value = Math.max(8, Math.min(top.value, innerHeight - height - 8));
  panel.value?.querySelector<HTMLInputElement>('input:not(:disabled)')?.focus();
}
async function set(role: string, checked: boolean) {
  if (busy.value) return;
  const previous = [...selected.value];
  selected.value = checked ? [...previous, role] : previous.filter((id) => id !== role);
  busy.value = true;
  error.value = '';
  try {
    await props.mutate('/api/members/' + props.member.id + '/roles', 'PUT', {
      roleIds: [...selected.value],
    });
  } catch (e) {
    selected.value = previous;
    error.value = (e as Error).message;
  } finally {
    busy.value = false;
  }
}
function outside(e: PointerEvent) {
  if (!panel.value?.contains(e.target as Node) && !trigger.value?.contains(e.target as Node))
    close();
}
function escape(e: KeyboardEvent) {
  if (open.value && e.key === 'Escape') {
    close();
    trigger.value?.focus();
  }
}
function scroll(e: Event) {
  if (!panel.value?.contains(e.target as Node)) close();
}
onMounted(() => {
  document.addEventListener('pointerdown', outside);
  document.addEventListener('keydown', escape);
  window.addEventListener('resize', close);
  window.addEventListener('scroll', scroll, true);
});
onBeforeUnmount(() => {
  document.removeEventListener('pointerdown', outside);
  document.removeEventListener('keydown', escape);
  window.removeEventListener('resize', close);
  window.removeEventListener('scroll', scroll, true);
});
</script>
<template>
  <button
    ref="trigger"
    type="button"
    class="soft-button"
    :aria-label="'Роли: ' + member.name"
    :aria-expanded="open"
    aria-haspopup="dialog"
    @click="toggle"
  >
    Роли</button
  ><Teleport to="body"
    ><div
      v-if="open"
      ref="panel"
      class="role-popover"
      role="dialog"
      :aria-label="'Роли: ' + member.name"
      :style="{ left: left + 'px', top: top + 'px' }"
    >
      <strong>{{ member.name }}</strong
      ><label
        v-for="role in roles.filter((r) => r.id !== 'member' && r.id !== 'owner')"
        :key="role.id"
        ><input
          type="checkbox"
          :checked="selected.includes(role.id)"
          :disabled="busy || !role.permissions.every((p) => permitted(self, p))"
          @change="set(role.id, ($event.target as HTMLInputElement).checked)"
        /><span>{{ role.name }}</span></label
      ><small v-if="roles.length < 2">Нет дополнительных ролей</small>
      <p v-if="error" role="alert">{{ error }}</p>
      <span class="sr-only" role="status">{{ busy ? 'Сохраняем роли…' : '' }}</span>
    </div></Teleport
  >
</template>
<style scoped>
.role-popover {
  position: fixed;
  z-index: 150;
  width: 240px;
  max-height: calc(100dvh - 16px);
  overflow: auto;
  background: #252525;
  border-radius: 12px;
  padding: 12px;
  box-shadow: 0 12px 32px #0008;
}
.role-popover > strong {
  display: block;
  margin: 3px 6px 10px;
  font-size: 12px;
}
.role-popover label {
  display: flex;
  align-items: center;
  gap: 10px;
  margin: 0;
  padding: 10px 6px;
  border-radius: 8px;
  font-size: 12px;
  cursor: pointer;
}
.role-popover label:hover {
  background: #303030;
}
.role-popover input {
  margin: 0;
  width: 16px;
  height: 16px;
  accent-color: #99b4aa;
}
.role-popover p {
  font-size: 11px;
  color: #efaaaa;
  margin-top: 8px;
}
.role-popover small {
  font-size: 11px;
  color: #999;
}
</style>
