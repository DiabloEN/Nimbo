<script setup lang="ts">
import AppIcon from './AppIcon.vue';
import {computed,nextTick,onBeforeUnmount,ref} from 'vue';
defineProps<{label?:string;icon?:string}>();const emit=defineEmits<{select:[emoji:string]}>();
const palette='😀 😃 😄 😁 😆 😅 😂 🤣 🙂 😉 😊 😍 🥰 😘 😎 🤔 😮 😢 😭 😡 🤯 🥳 😴 🤝 👍 👎 👏 🙌 🙏 💪 👀 ❤️ 💔 🔥 ✨ 🎉 💯 ✅ ❌ 🚀 ☕ 🎮 🐈 🐕 🌈 🎵'.split(' ');
const names='улыбка|радость|счастье|улыбка|смех|пот|слёзы смех|хохот|улыбка|подмигивание|смущение|любовь|любовь|поцелуй|круто|думаю|удивление|грусть|плач|злость|взрыв|праздник|сон|рукопожатие|нравится да|нет плохо|аплодисменты|ура|спасибо|сила|глаза|сердце любовь|разбитое сердце|огонь|искры|праздник|сто отлично|да готово|нет|ракета|кофе|игра|кот|собака|радуга|музыка'.split('|');
const open=ref(false),query=ref(''),trigger=ref<HTMLButtonElement>(),panel=ref<HTMLElement>(),search=ref<HTMLInputElement>(),left=ref(0),top=ref(0);
const filtered=computed(()=>palette.filter((e,i)=>!query.value||names[i]?.includes(query.value.toLowerCase())||e===query.value));
function close(){open.value=false;document.removeEventListener('pointerdown',outside);document.removeEventListener('keydown',escape);window.removeEventListener('resize',close);window.removeEventListener('scroll',onScroll,true);}
function outside(e:PointerEvent){if(!panel.value?.contains(e.target as Node)&&!trigger.value?.contains(e.target as Node))close();}
function escape(e:KeyboardEvent){if(e.key==='Escape'){close();trigger.value?.focus();}}
function onScroll(e:Event){if(!panel.value?.contains(e.target as Node))close();}
async function toggle(){
 if(open.value){close();return;}query.value='';open.value=true;
 await nextTick();if(!open.value||!panel.value||!trigger.value)return;
 const r=trigger.value.getBoundingClientRect(),p=panel.value.getBoundingClientRect();
 left.value=Math.max(8,Math.min(r.left,innerWidth-p.width-8));
 top.value=r.bottom+8+p.height<=innerHeight-8?r.bottom+8:Math.max(8,r.top-p.height-8);
 document.addEventListener('pointerdown',outside);document.addEventListener('keydown',escape);window.addEventListener('resize',close);window.addEventListener('scroll',onScroll,true);
 await nextTick();search.value?.focus({preventScroll:true});
}
function choose(emoji:string){emit('select',emoji);close();trigger.value?.focus();}
onBeforeUnmount(close);
</script>
<template>
 <button ref="trigger" type="button" class="emoji-trigger" :aria-label="label||'Добавить эмодзи'" :title="label||'Добавить эмодзи'" :aria-expanded="open" aria-haspopup="dialog" @click="toggle"><AppIcon :name="icon||'reaction-solid'" :size="20"/></button>
 <Teleport to="body"><section v-if="open" ref="panel" class="emoji-picker" role="dialog" aria-label="Эмодзи" :style="{left:left+'px',top:top+'px'}"><input ref="search" v-model="query" placeholder="Найти эмодзи" aria-label="Найти эмодзи"><div class="emoji-grid"><button v-for="emoji in filtered" :key="emoji" type="button" :title="names[palette.indexOf(emoji)]" :aria-label="names[palette.indexOf(emoji)]" @click="choose(emoji)">{{emoji}}</button><span v-if="!filtered.length">Ничего не найдено</span></div></section></Teleport>
</template>
<style>
.emoji-trigger{display:grid;place-items:center;flex-shrink:0;width:36px;height:36px;padding:0;background:transparent;color:var(--muted);border:0;border-radius:10px}.emoji-trigger:hover{background:#303030;color:white}.emoji-picker{position:fixed;z-index:200;width:min(288px,calc(100vw - 16px));padding:12px;border-radius:16px;background:#252525;box-shadow:0 12px 40px #0009}.emoji-picker input{width:100%;height:36px;margin:0 0 8px;padding:8px 12px;border:0;background:#181818}.emoji-grid{display:grid;grid-template-columns:repeat(6,minmax(0,1fr));gap:4px;max-height:min(224px,calc(100dvh - 84px));overflow-x:hidden;overflow-y:auto;scrollbar-width:thin;scrollbar-gutter:stable;overscroll-behavior:contain}.emoji-grid button{font-family:'Segoe UI Emoji','Apple Color Emoji',sans-serif;font-size:23px;line-height:1;width:100%;min-width:0;height:36px;padding:0;display:grid;place-items:center;background:transparent;border:0;border-radius:8px}.emoji-grid button:hover,.emoji-grid button:focus-visible{background:#444}.emoji-grid>span{grid-column:1/-1;padding:12px;font-size:12px;color:var(--muted)}
</style>
<style>.emoji-grid{height:min(224px,calc(100dvh - 84px));align-content:start}.emoji-grid::-webkit-scrollbar{width:4px}.emoji-grid::-webkit-scrollbar-thumb{background:#555;border-radius:4px}.emoji-grid::-webkit-scrollbar-button{display:none;width:0;height:0}</style>
