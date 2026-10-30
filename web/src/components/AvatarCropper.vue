<script setup lang="ts">
import {ref,onMounted,onBeforeUnmount,computed} from 'vue';
import {validAvatar} from '../composables/profileFile';
const props=defineProps<{file:File;square?:boolean}>();
const emit=defineEmits<{save:[value:string];cancel:[]}>();
const dialog=ref<HTMLDialogElement>(),canvas=ref<HTMLCanvasElement>(),zoom=ref(1),x=ref(0),y=ref(0),error=ref(''),ready=ref(false);
let bitmap:ImageBitmap|undefined,drag:{id:number;x:number;y:number;ox:number;oy:number}|undefined,disposed=false;
const scale=computed(()=>bitmap?Math.max(280/bitmap.width,280/bitmap.height)*zoom.value:1);
function draw(){if(!bitmap||!canvas.value)return;const w=bitmap.width*scale.value,h=bitmap.height*scale.value;x.value=Math.max((280-w)/2,Math.min((w-280)/2,x.value));y.value=Math.max((280-h)/2,Math.min((h-280)/2,y.value));const c=canvas.value.getContext('2d')!;c.clearRect(0,0,280,280);c.drawImage(bitmap,(280-w)/2+x.value,(280-h)/2+y.value,w,h);}
function down(e:PointerEvent){if(!ready.value)return;drag={id:e.pointerId,x:e.clientX,y:e.clientY,ox:x.value,oy:y.value};canvas.value?.setPointerCapture(e.pointerId);}
function move(e:PointerEvent){if(!drag||drag.id!==e.pointerId)return;const ratio=280/canvas.value!.getBoundingClientRect().width;x.value=drag.ox+(e.clientX-drag.x)*ratio;y.value=drag.oy+(e.clientY-drag.y)*ratio;draw();}
function key(e:KeyboardEvent){const step=e.shiftKey?20:5;if(e.key==='ArrowLeft')x.value-=step;else if(e.key==='ArrowRight')x.value+=step;else if(e.key==='ArrowUp')y.value-=step;else if(e.key==='ArrowDown')y.value+=step;else return;e.preventDefault();draw();}
function save(){if(!canvas.value||!ready.value)return;const out=document.createElement('canvas');out.width=out.height=192;out.getContext('2d')!.drawImage(canvas.value,0,0,192,192);const value=out.toDataURL('image/jpeg',.85);if(!validAvatar(value)){error.value='Не удалось подготовить аватар. Выберите другое изображение.';return;}emit('save',value);}
onMounted(async()=>{dialog.value?.showModal();try{if(!['image/jpeg','image/png','image/webp'].includes(props.file.type)||props.file.size>8*1024*1024)throw new Error('Выберите PNG, JPEG или WebP до 8 МБ.');const b=await createImageBitmap(props.file);if(disposed){b.close();return;}bitmap=b;ready.value=true;draw();}catch(e){error.value=(e as Error).message;}});
onBeforeUnmount(()=>{disposed=true;bitmap?.close();dialog.value?.close();});
</script>
<template><dialog ref="dialog" class="modal crop-dialog" aria-labelledby="crop-title" @cancel.prevent="emit('cancel')"><h2 id="crop-title">Кадрирование аватара</h2><p class="field-help">Переместите изображение и выберите масштаб.</p><div class="crop-preview" :class="{square}"><canvas ref="canvas" width="280" height="280" tabindex="0" aria-label="Область кадрирования. Перемещайте стрелками или перетаскиванием" @pointerdown="down" @pointermove="move" @pointerup="drag=undefined" @pointercancel="drag=undefined" @lostpointercapture="drag=undefined" @keydown="key"/></div><label class="crop-zoom">Масштаб <span>{{Math.round(zoom*100)}}%</span><input v-model.number="zoom" type="range" min="1" max="4" step="0.01" :disabled="!ready" @input="draw"></label><p v-if="error" class="form-error" role="alert">{{error}}</p><div class="button-row"><button class="primary" :disabled="!ready" @click="save">Применить</button><button @click="emit('cancel')">Отмена</button></div></dialog></template>
<style scoped>
.crop-dialog{width:min(400px,calc(100% - 32px));padding:28px}.crop-dialog h2{font-size:20px}.crop-preview{width:min(280px,100%);aspect-ratio:1;margin:auto;border-radius:50%;overflow:hidden;background:#252525;outline:1px solid #555}.crop-preview.square{border-radius:18px}.crop-preview canvas{width:100%;height:100%;display:block;cursor:grab;touch-action:none}.crop-preview canvas:active{cursor:grabbing}.crop-zoom{display:flex;flex-wrap:wrap;justify-content:space-between;gap:8px;margin:22px 0}.crop-zoom input{padding:0;accent-color:#66aaf0}.button-row{justify-content:flex-end}
</style>
