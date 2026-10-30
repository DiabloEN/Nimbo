<script setup lang="ts">
import {computed,onBeforeUnmount,onMounted,ref} from 'vue';
import type {Role,PostingPolicy} from '../composables/useWorkspace';
const props=defineProps<{modelValue:PostingPolicy;roles:Role[];disabled?:boolean}>();
const emit=defineEmits<{'update:modelValue':[PostingPolicy]}>();
const open=ref(false),root=ref<HTMLElement>(),trigger=ref<HTMLButtonElement>();
const summary=computed(()=>props.modelValue.mode==='everyone'?'Пишут все':props.modelValue.roleIds.length?`Пишут роли · ${props.modelValue.roleIds.length}`:'Только владелец');
function toggle(id:string,checked:boolean){emit('update:modelValue',{mode:'roles',roleIds:checked?[...new Set([...props.modelValue.roleIds,id])]:props.modelValue.roleIds.filter(r=>r!==id)});}
function outside(e:PointerEvent){if(!root.value?.contains(e.target as Node))open.value=false;}
function close(){open.value=false;trigger.value?.focus();}
onMounted(()=>document.addEventListener('pointerdown',outside));onBeforeUnmount(()=>document.removeEventListener('pointerdown',outside));
</script>
<template><div ref="root" class="channel-posting" @keydown.esc.stop.prevent="close"><button ref="trigger" type="button" class="posting-trigger" :disabled="disabled" aria-label="Кто может писать" aria-haspopup="dialog" :aria-expanded="open" @click="open=!open">{{summary}}<svg width="12" height="12" viewBox="0 0 16 16" fill="none" aria-hidden="true"><path d="m4 6 4 4 4-4" stroke="currentColor" stroke-linecap="round"/></svg></button><div v-if="open" class="posting-menu" role="dialog" aria-label="Кто может писать"><small>Кто может писать</small><label><input type="checkbox" :checked="modelValue.mode==='everyone'" :disabled="disabled" @change="emit('update:modelValue',{mode:($event.target as HTMLInputElement).checked?'everyone':'roles',roleIds:[]})">Все участники</label><label v-for="role in roles.filter(r=>r.id!=='owner')" :key="role.id"><input type="checkbox" :checked="modelValue.mode==='roles'&&modelValue.roleIds.includes(role.id)" :disabled="disabled" @change="toggle(role.id,($event.target as HTMLInputElement).checked)">{{role.name}}</label><small>Владелец всегда может писать.</small></div></div></template>
<style scoped>
.channel-posting{position:relative;flex:0 0 auto;min-width:0}.posting-trigger{display:flex;align-items:center;justify-content:space-between;gap:8px;font-size:11px;min-height:36px;background:#222;white-space:nowrap}.posting-menu{position:absolute;z-index:30;right:0;top:calc(100% + 6px);width:240px;max-height:300px;overflow:auto;padding:12px;background:#272727;border-radius:12px;box-shadow:0 12px 32px #0008;user-select:none}.posting-menu label{display:flex;align-items:center;gap:8px;margin:0;padding:9px 4px;color:#ddd;font-size:12px;cursor:pointer}.posting-menu input{flex:none;width:14px;min-width:14px;height:14px;margin:0}.posting-menu small{display:block;color:#999;font-size:11px;padding:4px;line-height:1.5}
</style>
