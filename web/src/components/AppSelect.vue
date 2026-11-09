<script setup lang="ts">
import AppIcon from './AppIcon.vue';
import { ref, computed, onMounted, onBeforeUnmount, watch, nextTick } from 'vue';
const props = defineProps<{
  id: string;
  label: string;
  modelValue: string;
  options: { value: string; label: string }[];
  disabled?: boolean;
  hideArrow?: boolean;
}>();
const emit = defineEmits<{ 'update:modelValue': [string] }>();
const open = ref(false),
  active = ref(0),
  root = ref<HTMLElement>(),
  trigger = ref<HTMLButtonElement>();
const chosen = computed(() => props.options.find((o) => o.value === props.modelValue));
function close() {
  open.value = false;
}
function toggle() {
  if (props.disabled) return;
  open.value = !open.value;
  active.value = Math.max(
    0,
    props.options.findIndex((o) => o.value === props.modelValue),
  );
}
function choose(index: number) {
  const option = props.options[index];
  if (option) {
    emit('update:modelValue', option.value);
    close();
    trigger.value?.focus();
  }
}
async function key(e: KeyboardEvent) {
  if (['ArrowDown', 'ArrowUp', 'Home', 'End', 'Escape', 'Enter', ' '].includes(e.key))
    e.preventDefault();
  if (e.key === 'Escape') {
    close();
    return;
  }
  if (e.key === 'Tab') {
    close();
    return;
  }
  if (e.key === 'Enter' || e.key === ' ') {
    if (open.value) choose(active.value);
    else toggle();
    return;
  }
  if (['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(e.key)) {
    if (!open.value) {
      toggle();
    } else
      active.value =
        e.key === 'Home'
          ? 0
          : e.key === 'End'
            ? props.options.length - 1
            : Math.max(
                0,
                Math.min(props.options.length - 1, active.value + (e.key === 'ArrowDown' ? 1 : -1)),
              );
    await nextTick();
    document
      .getElementById(`${props.id}-option-${active.value}`)
      ?.scrollIntoView({ block: 'nearest' });
  }
}
function outside(e: PointerEvent) {
  if (!root.value?.contains(e.target as Node)) close();
}
watch(
  () => props.disabled,
  (value) => {
    if (value) close();
  },
);
onMounted(() => document.addEventListener('pointerdown', outside));
onBeforeUnmount(() => document.removeEventListener('pointerdown', outside));
</script>
<template>
  <div ref="root" class="app-select" :class="{ open }">
    <button
      ref="trigger"
      :id="id"
      type="button"
      role="combobox"
      :aria-label="label"
      :aria-expanded="open"
      :aria-controls="`${id}-list`"
      :aria-activedescendant="open ? `${id}-option-${active}` : undefined"
      aria-haspopup="listbox"
      :disabled="disabled"
      :title="chosen?.label"
      @click="toggle"
      @keydown="key"
      @blur="close"
    >
      <span>{{ chosen?.label || 'Не выбрано' }}</span
      ><AppIcon v-if="!hideArrow" name="chevronDown" :size="16" />
    </button>
    <div v-if="open" :id="`${id}-list`" class="select-options" role="listbox" :aria-label="label">
      <div
        v-for="(option, index) in options"
        :id="`${id}-option-${index}`"
        :key="option.value"
        role="option"
        :aria-selected="modelValue === option.value"
        :class="{ highlight: active === index }"
        @pointerdown.prevent="choose(index)"
        @pointermove="active = index"
      >
        {{ option.label }}
      </div>
    </div>
  </div>
</template>
