<script setup lang="ts">
import AppIcon from './AppIcon.vue';
defineProps<{enabled?:boolean;available?:boolean;busy?:boolean}>();
const emit=defineEmits<{toggle:[]}>();
</script>
<template><button type="button" class="screen-audio-toggle" :class="{enabled}" :disabled="busy||!available" :aria-pressed="!!enabled" :aria-label="enabled?'Выключить звук демонстрации':'Передавать звук демонстрации'" :title="!available?'Звук не разрешён при выборе экрана':enabled?'Выключить звук демонстрации':'Передавать звук демонстрации'" @click="emit('toggle')"><AppIcon :name="enabled?'sound':'sound-off'" :size="18"/></button></template>
<style scoped>
.screen-audio-toggle{display:grid;place-items:center;flex:none;margin-left:auto;width:32px;height:32px;min-height:32px;padding:0;border-radius:8px;background:#282828;color:#aaa;transition:background .15s,color .15s}.screen-audio-toggle:hover{background:#393939;color:#eee}.screen-audio-toggle.enabled{background:#30413b;color:#d0e6de}.screen-audio-toggle.enabled:hover{background:#3c514a}.screen-audio-toggle:disabled{opacity:.45;cursor:default}
</style>
