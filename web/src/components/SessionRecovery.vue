<script setup lang="ts">
import AppIcon from './AppIcon.vue';
defineProps<{failed:boolean;busy:boolean}>();
defineEmits<{retry:[]}>();
</script>
<template><main class="auth-page"><section class="session-recovery" aria-labelledby="recovery-title"><span class="brand">nimbo<span>.</span></span><div class="modal-symbol"><AppIcon name="network" :size="26"/></div><h1 id="recovery-title">{{failed?'Восстанавливаем связь':'Восстанавливаем вход…'}}</h1><p class="field-help" role="status">{{failed?'Сервер пока не отвечает. Повторяем подключение автоматически; заново входить не нужно.':'Проверяем сохранённую сессию.'}}</p><button v-if="failed" class="soft-button" :disabled="busy" @click="$emit('retry')">{{busy?'Подключаемся…':'Повторить подключение'}}</button></section></main></template>
<style scoped>.session-recovery{width:min(400px,100%)}.brand{display:block;margin-bottom:32px;font-size:28px;letter-spacing:-1px}.modal-symbol{margin-bottom:20px}.session-recovery h1{font-size:24px;letter-spacing:-.4px}.field-help{margin:14px 0 22px}</style>
