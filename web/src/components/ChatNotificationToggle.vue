<script setup lang="ts">
import AppIcon from './AppIcon.vue';
defineProps<{ enabled: boolean }>();
defineEmits<{ toggle: [] }>();
</script>
<template>
  <button
    type="button"
    class="chat-notification-toggle"
    :class="{ enabled }"
    :aria-pressed="enabled"
    :aria-label="
      enabled
        ? 'Выключить уведомления обо всех сообщениях чата'
        : 'Включить уведомления обо всех сообщениях чата'
    "
    :title="
      enabled
        ? 'Все сообщения: звук включён'
        : 'Все сообщения: звук выключен. Упоминания и ответы оповещают всегда.'
    "
    @click="$emit('toggle')"
  >
    <span :class="{ muted: !enabled }"><AppIcon name="sound" :size="14" /></span>
  </button>
</template>
<style scoped>
.chat-notification-toggle {
  display: grid;
  place-items: center;
  width: 28px;
  height: 28px;
  min-width: 28px;
  padding: 0;
  margin-left: 2px;
  border-radius: 8px;
  background: transparent;
  color: #777;
}
.chat-notification-toggle.enabled {
  color: #cedbd7;
}
.chat-notification-toggle:hover {
  background: #292929;
  color: #fff;
}
.chat-notification-toggle > span {
  position: relative;
  display: grid;
  place-items: center;
  width: 14px;
  height: 14px;
}
.chat-notification-toggle .muted:after {
  content: '';
  position: absolute;
  width: 17px;
  height: 1.5px;
  transform: rotate(-45deg);
  background: currentColor;
  border-radius: 1px;
}
</style>
