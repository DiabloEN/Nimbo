<script setup lang="ts">
import {computed,nextTick,onBeforeUnmount,ref,useId,watch} from 'vue';
import type {Member} from '../composables/useWorkspace';
const props=defineProps<{emoji:string;users:string[];members:Member[];selfId:string;disabled?:boolean}>();
const emit=defineEmits<{toggle:[]}>();
const anchor=ref<HTMLElement>(),popup=ref<HTMLElement>(),open=ref(false),position=ref({left:'0px',top:'0px'}),id=useId();
const people=computed(()=>props.users.map(id=>({id,name:props.members.find(m=>m.id===id)?.name||'Участник'})));
let timer:ReturnType<typeof setTimeout>|undefined;
function cancel(){clearTimeout(timer);}
function place(){
 const a=anchor.value?.getBoundingClientRect(),p=popup.value;if(!a||!p)return;
 const width=p.offsetWidth,height=p.offsetHeight;
 position.value={left:Math.max(8,Math.min(innerWidth-width-8,a.left))+'px',top:Math.max(8,Math.min(innerHeight-height-8,a.top-height-8>=8?a.top-height-8:a.bottom+8))+'px'};
}
function enter(){cancel();if(open.value)return;timer=setTimeout(async()=>{open.value=true;await nextTick();place();},180);}
function leave(){cancel();timer=setTimeout(()=>open.value=false,140);}
function close(){cancel();open.value=false;}
function scroll(e:Event){if(popup.value?.contains(e.target as Node))return;close();}
watch(open,v=>{if(v){window.addEventListener('resize',close);document.addEventListener('scroll',scroll,true);}else{window.removeEventListener('resize',close);document.removeEventListener('scroll',scroll,true);}});
watch(()=>props.users.length,()=>{if(open.value)void nextTick(place);});
onBeforeUnmount(()=>{cancel();window.removeEventListener('resize',close);document.removeEventListener('scroll',scroll,true);});
</script>
<template>
 <span ref="anchor" class="reaction-anchor" @mouseenter="enter" @mouseleave="leave" @focusin="enter" @focusout="leave" @keydown.esc.stop="close" @contextmenu.stop>
  <button type="button" class="reaction-chip" :class="{selected:users.includes(selfId)}" :aria-pressed="users.includes(selfId)" :aria-label="`${emoji}: ${users.length}. ${users.includes(selfId)?'Убрать':'Добавить'} реакцию`" :aria-describedby="open?id:undefined" :aria-disabled="disabled||undefined" @click="!disabled&&emit('toggle')"><span>{{emoji}}</span>{{users.length}}</button>
 </span>
 <Teleport to="body"><div v-if="open" :id="id" ref="popup" class="reaction-people" role="tooltip" :style="position" @mouseenter="cancel" @mouseleave="leave" @contextmenu.stop>
  <ul><li v-for="person in people" :key="person.id"><span class="reaction-person-name" :class="{self:person.id===selfId}">{{person.name}}</span></li></ul>
 </div></Teleport>
</template>
<style scoped>
.reaction-anchor{display:inline-flex}.reaction-chip{display:flex;align-items:center;gap:5px;padding:5px 9px;border:0;border-radius:10px;background:#242424;color:#aaa;font-size:12px;transition:background .14s,color .14s}.reaction-chip>span{font-size:17px;line-height:22px}.reaction-chip.selected{background:#344746;color:#d1f5ed}.reaction-chip:hover,.reaction-chip:focus-visible{background:#353535;color:#eee}.reaction-chip.selected:hover,.reaction-chip.selected:focus-visible{background:#425a55}.reaction-chip[aria-disabled=true]{cursor:default}
.reaction-people{position:fixed;z-index:250;width:max-content;max-width:min(320px,calc(100vw - 16px));max-height:min(240px,calc(100dvh - 16px));padding:10px 12px;background:#252525;color:#eee;border-radius:12px;box-shadow:0 10px 32px #0007;box-sizing:border-box;overflow:auto;scrollbar-width:thin;scrollbar-color:#555 transparent}.reaction-people ul{list-style:none;margin:0;padding:0;font-size:12px;line-height:20px;overflow-wrap:anywhere}.reaction-people li{display:inline}.reaction-people li+li:before{content:' · ';color:#888}.reaction-person-name.self{color:#a9d8cb}
</style>
