<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount } from 'vue';
import { channelIcons } from '../composables/channelIcons';
import AppIcon from './AppIcon.vue';
defineProps<{ modelValue: string; disabled?: boolean }>();
const emit = defineEmits<{ 'update:modelValue': [value: string] }>();
const open = ref(false),
  root = ref<HTMLElement>();
function outside(e: PointerEvent) {
  if (!root.value?.contains(e.target as Node)) open.value = false;
}
onMounted(() => document.addEventListener('pointerdown', outside));
onBeforeUnmount(() => document.removeEventListener('pointerdown', outside));
</script>
<template>
  <div ref="root" class="channel-icon-picker" @keydown.esc.stop="open = false">
    <button
      type="button"
      aria-label="Выбрать иконку канала"
      :aria-expanded="open"
      :disabled="disabled"
      @click="open = !open"
    >
      <AppIcon :name="modelValue" :size="20" />
    </button>
    <div v-if="open" class="channel-icon-options" role="group" aria-label="Иконки канала">
      <button
        v-for="icon in channelIcons"
        :key="icon.id"
        type="button"
        :aria-label="icon.name"
        :title="icon.name"
        :aria-pressed="modelValue === icon.id"
        @click="
          emit('update:modelValue', icon.id);
          open = false;
        "
      >
        <AppIcon :name="icon.id" :size="22" />
      </button>
    </div>
  </div>
</template>
<style scoped>
.channel-icon-picker {
  position: relative;
  flex: none;
}
.channel-icon-options {
  position: absolute;
  top: calc(100% + 8px);
  left: 0;
  z-index: 20;
  width: 224px;
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 5px;
  padding: 10px;
  background: #2b2b2b;
  border-radius: 14px;
  box-shadow: 0 12px 25px #0008;
}
.channel-icon-options button {
  padding: 10px;
  background: transparent;
}
.channel-icon-options button[aria-pressed='true'] {
  background: #4a4a4a;
}
.channel-icon-options button:hover {
  background: #444;
}
</style>
