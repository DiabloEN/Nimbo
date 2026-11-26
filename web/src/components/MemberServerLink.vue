<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, ref } from 'vue';
import { returnLink } from '../composables/serverLinks';
import AppIcon from './AppIcon.vue';
const props = defineProps<{ origin: string }>();
const url = computed(() => returnLink(props.origin));
const copied = ref(false);
const manual = ref(false);
const field = ref<HTMLInputElement>();
let timer: ReturnType<typeof setTimeout> | undefined;
onBeforeUnmount(() => clearTimeout(timer));
async function copy() {
  try {
    await navigator.clipboard.writeText(url.value);
    manual.value = false;
    copied.value = true;
    clearTimeout(timer);
    timer = setTimeout(() => (copied.value = false), 2000);
  } catch {
    manual.value = true;
    await nextTick();
    field.value?.focus();
    field.value?.select();
  }
}
</script>
<template>
  <div class="member-server-link">
    <button type="button" class="soft-button" title="Скопировать ссылку на сервер" @click="copy">
      <span class="link-label">
        <AppIcon name="copy" :size="13" />
        <span aria-live="polite">{{ copied ? 'Скопировано' : 'Ссылка на сервер' }}</span>
      </span>
    </button>
    <input
      v-if="manual"
      ref="field"
      :value="url"
      readonly
      aria-label="Ссылка на сервер для копирования"
      @focus="field?.select()"
    />
  </div>
</template>
<style scoped>
.member-server-link {
  flex: none;
  padding: 8px;
}
.member-server-link button {
  display: grid;
  place-items: center;
  width: 100%;
  min-height: 36px;
  padding: 6px 8px;
  font-size: 10.5px;
}
.link-label {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  white-space: nowrap;
}
.member-server-link input {
  width: 100%;
  margin-top: 6px;
}
</style>
