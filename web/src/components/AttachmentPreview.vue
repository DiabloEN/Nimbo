<script setup lang="ts">
import {ref,computed,watch,onMounted,onBeforeUnmount,nextTick} from 'vue';
import AppIcon from './AppIcon.vue';
import AudioAttachment from './AudioAttachment.vue';
import {attachmentMedia,validMediaHeader} from '../composables/attachmentMedia';
const props=defineProps<{name:string;gallery?:{name:string;src:string}[];galleryIndex?:number;src?:string;compact?:boolean;file?:File;load?:()=>Promise<Blob>;draft?:boolean;size?:number;scanned?:boolean;downloading?:boolean}>();
const emit=defineEmits<{download:[index?:number]}>();
const viewer=ref<HTMLDialogElement>(),expanded=ref(false),inlineVideo=ref<HTMLVideoElement>();
const aspect=ref(16/9);
const frameStyle=computed(()=>({width:Math.min(props.compact?240:480,(props.compact?120:280)*aspect.value)+'px',aspectRatio:String(aspect.value)}));
function measured(e:Event){const el=e.target as HTMLImageElement&HTMLVideoElement;const w=el.naturalWidth||el.videoWidth,h=el.naturalHeight||el.videoHeight;if(w&&h)aspect.value=w/h;loading.value=false;}
const keyboardControls=ref(false);
const viewIndex=ref(0),viewVideo=ref<HTMLVideoElement>();
const viewItem=computed(()=>props.gallery?.[viewIndex.value]||{name:props.name,src:url.value});
const viewKind=computed(()=>attachmentMedia(viewItem.value.name)?.kind);
const galleryCount=computed(()=>props.gallery?.length||1);
function step(delta:number){const next=viewIndex.value+delta;if(next<0||next>=galleryCount.value)return;viewVideo.value?.pause();viewIndex.value=next;}
function viewerKey(e:KeyboardEvent){keyboardControls.value=true;if((e.target as HTMLElement).closest('video,input,textarea'))return;if(e.key==='ArrowLeft'||e.key==='ArrowRight'){e.preventDefault();step(e.key==='ArrowLeft'?-1:1);}}
const visual=computed(()=>media.value?.kind==='image'||media.value?.kind==='video');
async function open(){if(props.draft||!url.value)return;inlineVideo.value?.pause();viewIndex.value=props.galleryIndex||0;keyboardControls.value=false;expanded.value=true;await nextTick();viewer.value?.showModal();}
function close(){viewVideo.value?.pause();viewer.value?.close();expanded.value=false;}
function backdrop(e:MouseEvent){if(e.target!==viewer.value)return;const r=viewer.value.getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)close();}
function sizeLabel(){const bytes=props.size||props.file?.size||0;return bytes<1024*1024?`${(bytes/1024).toFixed(1)} КБ`:`${(bytes/1024/1024).toFixed(2)} МБ`;}
const host=ref<HTMLElement>(),url=ref(''),error=ref(''),loading=ref(false),visible=ref(false);
const media=computed(()=>attachmentMedia(props.name));let generation=0,observer:IntersectionObserver|undefined;
function clean(){if(url.value.startsWith('blob:'))URL.revokeObjectURL(url.value);url.value='';}
async function prepare(){
 const version=++generation;close();clean();error.value='';loading.value=false;
 if(!media.value||(!props.file&&!visible.value))return;
 loading.value=true;
 if(props.src&&!props.file){url.value=props.src;return;}
 try{const blob=props.file||await props.load?.();if(version!==generation||!blob)return;
  if(blob.size>20*1024*1024||!await validMediaHeader(blob,props.name))throw Error();
  if(version!==generation)return;
  url.value=URL.createObjectURL(blob.slice(0,blob.size,media.value.mime));
 }catch{if(version===generation)error.value='Предпросмотр недоступен. Файл можно скачать.';}finally{if(version===generation)loading.value=false;}
}
watch(()=>[props.file,props.name,props.src,visible.value],()=>void prepare(),{immediate:true});
onMounted(()=>{if(host.value){observer=new IntersectionObserver(entries=>{if(entries.some(e=>e.isIntersecting)){visible.value=true;observer?.disconnect();}},{rootMargin:'100px'});observer.observe(host.value);}});
onBeforeUnmount(()=>{generation++;observer?.disconnect();close();clean();});
function failed(){error.value='Этот формат не удалось показать. Файл можно скачать.';clean();}
</script>
<template>
 <div ref="host" class="attachment-preview" :class="{draft,visual,compact}">
  <template v-if="draft">
   <img v-if="url&&media?.kind==='image'" :src="url" alt="" @error="failed">
   <video v-else-if="url&&media?.kind==='video'" :src="url" muted playsinline preload="metadata" @error="failed"/>
   <AppIcon v-else name="paperclip" :size="22"/>
  </template>
  <template v-else>
   <div v-if="visual&&!error" class="media-frame" :style="frameStyle" :class="{'media-video':media?.kind==='video','media-image':media?.kind==='image'}" :aria-busy="loading">
    <button v-if="media?.kind==='image'" type="button" class="image-open" :disabled="!url" :aria-label="'Открыть '+name" @click="open"><img v-if="url" :src="url" :alt="name" @load="measured" @error="failed"><span v-else class="media-placeholder" aria-label="Изображение"/></button>
    <button v-else-if="compact" type="button" class="video-open" :disabled="!url" :aria-label="'Открыть видео '+name" @click="open"><video :src="url||undefined" muted playsinline preload="metadata" @loadedmetadata="measured" @loadeddata="loading=false" @error="failed"/><svg class="play-symbol" width="28" height="28" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M8 5v14l11-7z"/></svg></button>
    <video v-else ref="inlineVideo" :src="url||undefined" controls playsinline preload="metadata" :aria-label="name" @loadedmetadata="measured" @loadeddata="loading=false" @dblclick="open" @error="failed"/>
    <div class="media-tools">
     <button v-if="media?.kind==='video'&&!compact" type="button" class="media-action" aria-label="Открыть видео крупно" title="Открыть видео крупно" @click="open"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5"/></svg></button>
     <button v-if="media?.kind==='image'" type="button" class="media-action" :disabled="downloading" :aria-label="'Скачать '+name" :title="downloading?'Загрузка…':'Скачать'" @click="emit('download')"><AppIcon name="download" :size="18"/></button>
    </div>
   </div>
   <template v-else>
    <AudioAttachment v-if="media?.kind==='audio'&&!error" :src="url" :name="name" :downloading="downloading" @download="emit('download')" @error="failed"/>
    <button v-else type="button" class="file-download" :disabled="downloading" @click="emit('download')"><AppIcon name="download" :size="20"/><span>{{name}}<small>{{sizeLabel()}} · {{scanned?'Проверен антивирусом':'Без антивирусной проверки'}}</small></span><small>{{downloading?'Загрузка…':'Скачать'}}</small></button>
    <p v-if="error" class="field-help">{{error}}</p>
   </template>
  </template>
 </div>
 <Teleport to="body"><dialog v-if="expanded" ref="viewer" class="attachment-viewer" :class="{'keyboard-controls':keyboardControls}" :aria-label="viewItem.name" @pointermove="keyboardControls=false" @keydown="viewerKey" @cancel.prevent="close" @click="backdrop" @close="expanded=false">
  <header class="viewer-control"><span class="viewer-name" :title="viewItem.name">{{viewItem.name}}</span><small v-if="galleryCount>1" aria-live="polite">{{viewIndex+1}} / {{galleryCount}}</small><button type="button" class="media-action" aria-label="Закрыть просмотр" autofocus @click="close"><AppIcon name="close" :size="22"/></button></header>
  <img v-if="viewKind==='image'" :src="viewItem.src" :alt="viewItem.name"><video v-else :key="viewItem.src" ref="viewVideo" :src="viewItem.src" controls playsinline preload="metadata" :aria-label="viewItem.name"/>
  <nav v-if="galleryCount>1" class="gallery-navigation" aria-label="Вложения сообщения"><button type="button" class="media-action viewer-control gallery-previous" :disabled="viewIndex===0" aria-label="Предыдущее вложение" @click="step(-1)"><svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="m14 5-7 7 7 7"/></svg></button><button type="button" class="media-action viewer-control gallery-next" :disabled="viewIndex===galleryCount-1" aria-label="Следующее вложение" @click="step(1)"><svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="m10 5 7 7-7 7"/></svg></button></nav>
  <button v-if="viewKind==='image'" type="button" class="media-action viewer-control viewer-download" :disabled="downloading" aria-label="Скачать файл" title="Скачать" @click="emit('download',gallery?viewIndex:undefined)"><AppIcon name="download" :size="20"/></button>
 </dialog></Teleport>
</template>
<style scoped>
.attachment-preview{width:fit-content;max-width:480px;margin:8px 0}.media-frame{position:relative;width:fit-content;max-width:100%;border-radius:12px;overflow:hidden;background:#111}.media-frame img,.media-frame video{display:block;max-width:100%;max-height:300px;object-fit:contain}.image-open{display:block;padding:0;border:0;border-radius:0;background:none;cursor:zoom-in}.image-open:focus-visible{outline:2px solid #8fc4f5;outline-offset:-2px}.media-tools{position:absolute;top:8px;right:8px;display:flex;gap:6px;opacity:0;transition:opacity .12s}.media-frame:hover .media-tools,.media-frame:focus-within .media-tools{opacity:1}.media-action{display:flex;align-items:center;justify-content:center;width:34px;height:34px;flex:none;padding:0;border:0;border-radius:9px;background:#171717dc;color:#eee;cursor:pointer}.media-action:hover{background:#393939}.media-action:focus-visible{outline:2px solid #8fc4f5}.attachment-preview audio{display:block;width:min(360px,100%);height:40px}.file-download{display:flex;gap:12px;align-items:center;width:100%;text-align:left;background:#242424;padding:12px;border-radius:12px}.file-download>span{min-width:0;overflow-wrap:anywhere;flex:1}.file-download small{display:block;font-size:10px;color:#aaa;margin-top:4px}.file-download>small{color:#8fc4f5}.attachment-preview.draft{flex:none;display:flex;align-items:center;justify-content:center;width:60px;height:60px;margin:0;border-radius:8px;background:#161616;overflow:hidden;color:#aaa}.draft img,.draft video{display:block;width:100%;height:100%;object-fit:contain}.attachment-preview:not(.visual):not(.draft){width:480px;max-width:100%}.attachment-preview .field-help{margin:4px 0}.attachment-viewer{position:fixed;inset:0;margin:auto;border:0;padding:0;border-radius:16px;color:#eee;background:#111;max-width:96vw;max-height:96dvh;box-sizing:border-box;overflow:hidden}.attachment-viewer::backdrop{background:#000d}.attachment-viewer header{position:absolute;inset:0 0 auto;z-index:2;display:flex;align-items:center;gap:12px;padding:12px;background:linear-gradient(#000a,transparent);margin:0}.viewer-name{flex:1;min-width:0;white-space:nowrap;text-overflow:ellipsis;overflow:hidden;font-size:12px;user-select:text;cursor:text;text-shadow:0 1px 3px #000}.attachment-viewer header small{white-space:nowrap;color:#ddd;text-shadow:0 1px 3px #000}.attachment-viewer>img,.attachment-viewer>video{display:block;max-width:96vw;max-height:96dvh;object-fit:contain;margin:0}.attachment-viewer>video{width:1200px}.viewer-control{opacity:0;pointer-events:none;transition:opacity .14s ease}.attachment-viewer:hover .viewer-control,.attachment-viewer.keyboard-controls .viewer-control{opacity:1;pointer-events:auto}.gallery-navigation{position:absolute;inset:0;pointer-events:none}.gallery-navigation button{position:absolute;top:50%;transform:translateY(-50%);width:42px;height:48px;background:#1119}.gallery-previous{left:12px}.gallery-next{right:12px}.gallery-navigation button:disabled{visibility:hidden;pointer-events:none}.viewer-download{position:absolute;right:12px;bottom:12px}.attachment-viewer .media-action{background:#1119;backdrop-filter:blur(8px)}.attachment-viewer .media-action:hover{background:#333c}@media(hover:none){.attachment-viewer .viewer-control{opacity:1;pointer-events:auto}}@media(prefers-reduced-motion:reduce){.viewer-control{transition:none}}@media(hover:none){.media-tools{opacity:1}}@media(prefers-reduced-motion:reduce){.media-tools{transition:none}}
.video-open{position:relative;display:block;border:0;padding:0;width:100%;height:100%;background:#111;cursor:pointer}.play-symbol{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);padding:5px;box-sizing:content-box;border-radius:50%;background:#0009;color:#fff}.video-open:focus-visible{outline:2px solid #8fc4f5;outline-offset:-2px}
</style>

<style scoped>
.media-frame.media-image,.media-frame.media-video{max-width:100%;height:auto}.media-image .image-open{width:100%;height:100%}.media-frame img,.media-frame video{width:100%;height:100%;object-fit:contain}.media-placeholder{display:block;width:100%;height:100%;background:#171717}.compact.attachment-preview{width:fit-content;max-width:100%;margin:0}.compact .media-frame.media-image,.compact .media-frame.media-video{height:auto}.media-tools button:disabled{cursor:default}.image-open:disabled{cursor:default}
</style>
