<script setup lang="ts">
import {ref,computed,watch,nextTick,onMounted,onBeforeUnmount} from 'vue';
import {activeServer} from '../composables/serverConnection';
import AppIcon from './AppIcon.vue';import {type SavedServer}from '../composables/profileFile';
const props=defineProps<{servers:SavedServer[]}>();
const open=ref(false),root=ref<HTMLElement>(),trigger=ref<HTMLButtonElement>(),list=ref<HTMLElement>();
const scrollTop=ref(0),moreBelow=ref(false);
const origin=activeServer;
const emit=defineEmits<{open:[server:SavedServer];refresh:[]}>();const sorted=computed(()=>[...props.servers].sort((a,b)=>Number(!!b.pinned)-Number(!!a.pinned)));
function measure(){const el=list.value;if(el){scrollTop.value=el.scrollTop;moreBelow.value=el.scrollTop+el.clientHeight<el.scrollHeight-2;}}
watch(open,value=>{if(value)emit('refresh');});
watch([open,()=>props.servers.length],async()=>{await nextTick();measure();});
function tilt(index:number){return Math.max(0,Math.min(3,index-scrollTop.value/66))*4+'deg';}
function outside(e:PointerEvent){if(!root.value?.contains(e.target as Node))open.value=false;}
function close(){open.value=false;trigger.value?.focus();}
onMounted(()=>{document.addEventListener('pointerdown',outside);window.addEventListener('resize',measure);});
onBeforeUnmount(()=>{document.removeEventListener('pointerdown',outside);window.removeEventListener('resize',measure);});
</script>
<template>
 <div v-if="servers.length>1" ref="root" class="server-switcher" @keydown.esc.stop="close">
  <button ref="trigger" class="server-switch-trigger" aria-label="Выбрать сервер" title="Выбрать сервер" :aria-expanded="open" @click="open=!open">
   <svg width="19" height="19" viewBox="0 0 24 24" fill="none" aria-hidden="true"><rect x="3" y="3.5" width="18" height="7" rx="2.5" stroke="currentColor" stroke-width="1.5"/><rect x="3" y="13.5" width="18" height="7" rx="2.5" stroke="currentColor" stroke-width="1.5"/><circle cx="7" cy="7" r="1" fill="currentColor"/><circle cx="7" cy="17" r="1" fill="currentColor"/><path d="M12 7h5m-5 10h5" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/></svg>
  </button>
  <Transition name="server-menu"><nav v-if="open" class="server-switch-menu" aria-label="Мои серверы">
   <div ref="list" class="server-switch-list" :class="{'fade-top':scrollTop>2,'fade-bottom':moreBelow}" @scroll.passive="measure">
    <a v-for="(s,i) in sorted" :key="s.url" :style="{'--tilt':tilt(i)}" :href="'#/room'" :aria-current="s.url===origin?'page':undefined" @click.prevent="open=false;emit('open',s)">
     <span class="server-initial">{{Array.from(s.name)[0]}}</span><span><b>{{s.name}}</b><small>{{s.url.replace(/^https?:\/\//,'')}}</small></span><AppIcon v-if="s.pinned" name="pin" :size="14"/>
    </a>
   </div>
  </nav></Transition>
 </div>
</template>
<style scoped>
.server-switcher{position:relative;flex:none}
.server-switch-trigger{display:grid;place-items:center;width:26px;height:26px;padding:0;background:transparent;border-radius:6px;color:#888}
.server-switch-trigger:hover,.server-switch-trigger[aria-expanded=true]{background:#262626;color:#ddd}
.server-switch-menu{position:absolute;top:-8px;left:calc(100% + 10px);width:306px;max-width:calc(100vw - 32px);z-index:40}
.server-switch-list{max-height:min(270px,calc(100dvh - 90px));overflow-y:auto;overflow-x:hidden;overscroll-behavior:contain;scrollbar-width:none;padding:8px 34px 36px 8px;--fade-start:#000;--fade-end:#000;mask-image:linear-gradient(to bottom,var(--fade-start),#000 18px,#000 calc(100% - 58px),var(--fade-end))}
.server-switch-list.fade-top{--fade-start:transparent}.server-switch-list.fade-bottom{--fade-end:transparent}
.server-switch-list::-webkit-scrollbar{display:none}
.server-switch-list a{position:relative;display:flex;align-items:center;gap:10px;height:64px;margin:0 0 2px;padding:10px 12px;border-radius:12px;background:#252525;box-shadow:0 3px 12px #0005;transform:rotate(var(--tilt));transform-origin:24px 0;transition:transform 160ms ease,background 160ms ease}
.server-switch-list a:last-child{margin-bottom:0}
.server-switch-list a:hover,.server-switch-list a:focus-visible{background:#373737;transform:rotate(0);z-index:1;outline-offset:2px}
.server-switch-list a[aria-current]{background:#343c39}
.server-switch-list a>span:nth-child(2){min-width:0;flex:1}
.server-switch-list b{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:12px;font-weight:500}
.server-switch-list small{display:block;font-size:10px;color:#999;margin-top:4px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.server-initial{display:grid;place-items:center;width:34px;height:34px;flex:none;border-radius:10px;background:#ddd;color:#222;font-size:16px}
.server-menu-enter-active,.server-menu-leave-active{transition:opacity 140ms,transform 140ms;transform-origin:top left}
.server-menu-enter-from,.server-menu-leave-to{opacity:0;transform:translateX(-6px) scale(.97)}
@media(max-width:800px){.server-switch-menu{top:calc(100% + 6px);left:auto;right:-8px}}
@media(prefers-reduced-motion:reduce){.server-menu-enter-active,.server-menu-leave-active,.server-switch-list a{transition:none}}
</style>
