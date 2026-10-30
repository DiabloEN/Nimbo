<script setup lang="ts">
import {ref,onMounted,onBeforeUnmount,watch} from 'vue';
defineOptions({inheritAttrs:false});
const props=defineProps<{modelValue:string;disabled?:boolean}>();
const emit=defineEmits<{ 'update:modelValue':[value:string] }>();
const visible=ref(false);
function hide(){visible.value=false;}
function press(e:PointerEvent){if(e.button===0&&!props.disabled){e.preventDefault();visible.value=true;}}
function key(e:KeyboardEvent){if((e.key===' '||e.key==='Enter')&&!props.disabled){e.preventDefault();visible.value=true;}}
watch(()=>props.disabled,hide);
onMounted(()=>{window.addEventListener('pointerup',hide);window.addEventListener('pointercancel',hide);window.addEventListener('blur',hide);document.addEventListener('visibilitychange',hide);});
onBeforeUnmount(()=>{window.removeEventListener('pointerup',hide);window.removeEventListener('pointercancel',hide);window.removeEventListener('blur',hide);document.removeEventListener('visibilitychange',hide);});
</script>
<template><span class="password-input"><input v-bind="$attrs" :value="modelValue" :type="visible?'text':'password'" :disabled="disabled" @input="emit('update:modelValue',($event.target as HTMLInputElement).value)"><button type="button" :disabled="disabled" aria-label="Удерживайте, чтобы показать пароль" title="Удерживайте, чтобы показать пароль" @pointerdown="press" @pointerleave="hide" @pointerup="hide" @pointercancel="hide" @keydown="key" @keyup="hide" @blur="hide" @click.prevent><svg width="18" height="18" viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12Z" stroke="currentColor" stroke-width="1.5"/><circle cx="12" cy="12" r="3" stroke="currentColor" stroke-width="1.5"/></svg></button></span></template>
<style scoped>
.password-input{display:block;position:relative;margin-top:7px}.password-input>input{display:block;width:100%;padding-right:44px!important;margin:0!important}.password-input>button{position:absolute;right:6px;top:50%;transform:translateY(-50%);display:grid;place-items:center;width:32px;height:32px;min-height:0;padding:0;border-radius:7px;background:transparent;color:#999;touch-action:none;user-select:none}.password-input>button:hover,.password-input>button:focus-visible{color:#ddd;background:#ffffff09}
</style>
