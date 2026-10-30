<script setup lang="ts">
import {ref,onMounted,onBeforeUnmount} from 'vue';
import EffectPicker from './EffectPicker.vue';import AppIcon from './AppIcon.vue';
defineProps<{enabled:boolean;busy:boolean;note:string;strength?:number}>();const emit=defineEmits<{strength:[value:number];select:[value:boolean]}>();
const open=ref(false),root=ref<HTMLElement>(),trigger=ref<HTMLButtonElement>();
function outside(e:PointerEvent){if(!root.value?.contains(e.target as Node))open.value=false;}
function close(){open.value=false;trigger.value?.focus();}
onMounted(()=>document.addEventListener('pointerdown',outside));onBeforeUnmount(()=>document.removeEventListener('pointerdown',outside));
</script>
<template><div ref="root" class="quick-effects" @keydown.esc.stop="close"><button ref="trigger" type="button" aria-label="Эффекты камеры" title="Эффекты камеры" :aria-expanded="open" aria-controls="quick-effects-panel" :class="{active:enabled}" @click="open=!open"><AppIcon name="sparkles" :size="21"/></button><div v-if="open" id="quick-effects-panel" class="quick-effects-panel" role="group" aria-label="Эффекты камеры"><header><strong role="status">{{busy?'Применяем эффект…':'Эффекты камеры'}}</strong><button type="button" aria-label="Закрыть эффекты" @click="close"><AppIcon name="close" :size="18"/></button></header><EffectPicker :strength="strength" @strength="emit('strength',$event)" :enabled="enabled" :busy="busy" @select="emit('select',$event)"/><p v-if="note" class="field-help" role="status">{{note}}</p></div></div></template>
<style scoped>
.quick-effects{position:relative}.quick-effects-panel{position:absolute;bottom:calc(100% + 12px);left:0;width:min(380px,calc(100vw - 40px));padding:16px;border-radius:16px;background:#202020;box-shadow:0 12px 40px #0008;z-index:30}.quick-effects-panel header{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;font-size:13px}.quick-effects-panel header button{width:28px;height:28px;padding:4px;background:transparent}@media(max-width:1050px){.quick-effects-panel{left:auto;right:0}}
@media(max-width:700px){.quick-effects-panel{position:fixed;left:16px;right:auto;bottom:88px;width:calc(100vw - 32px);max-width:380px}}
</style>
