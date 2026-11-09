<script setup lang="ts">
import { ref, watch, onBeforeUnmount } from 'vue';
const props = defineProps<{ message: string; activity: string; active: boolean }>();
const visible = ref('');
let timer: ReturnType<typeof setTimeout> | undefined;
watch(
  () => [props.message, props.activity, props.active],
  () => {
    clearTimeout(timer);
    if (!props.active || !props.message) {
      visible.value = '';
      return;
    }
    if (props.activity) return;
    visible.value = props.message;
    timer = setTimeout(() => {
      visible.value = '';
    }, 10000);
  },
  { immediate: true },
);
onBeforeUnmount(() => clearTimeout(timer));
</script>
<template>
  <div class="dock-notice-slot">
    <Transition name="dock-notice"
      ><div
        v-if="visible"
        :key="visible"
        class="dock-notice"
        role="status"
        aria-live="polite"
        :title="visible"
      >
        <span>{{ visible }}</span>
      </div></Transition
    >
  </div>
</template>
<style scoped>
.dock-notice-slot {
  flex: 1;
  min-width: 0;
  height: 46px;
  position: relative;
  overflow: hidden;
}
.dock-notice {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: safe center;
  margin: 0;
  font-size: 11px;
  line-height: 15px;
  color: var(--muted);
  overflow: auto;
  overflow-wrap: anywhere;
  scrollbar-width: thin;
}
.dock-notice-enter-active,
.dock-notice-leave-active {
  transition:
    opacity 180ms ease,
    transform 180ms ease;
}
.dock-notice-enter-from,
.dock-notice-leave-to {
  opacity: 0;
  transform: translateY(8px);
}
@media (prefers-reduced-motion: reduce) {
  .dock-notice-enter-active,
  .dock-notice-leave-active {
    transition: none;
  }
}
</style>
