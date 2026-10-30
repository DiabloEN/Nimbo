<script setup lang="ts">
import {remoteServer} from '../composables/serverConnection';
import {computed,nextTick,onBeforeUnmount,onMounted,ref,watch} from 'vue';
import type {Message,Member,Role,PendingMessage} from '../composables/useWorkspace';
import {mentionChoices,moveMentions,withEveryone,messageParts,type Mention,type MentionChoice} from '../composables/mentions';
import EmojiPicker from './EmojiPicker.vue';
import ReactionButton from './ReactionButton.vue';
import AppIcon from './AppIcon.vue';
import LinkedText from './LinkedText.vue';
import {attachmentMedia} from '../composables/attachmentMedia';
import AttachmentPreview from './AttachmentPreview.vue';
import {fileSelectionError,preventFileNavigation} from '../composables/chatFiles';
import {useVisibleMentions} from '../composables/useVisibleMentions';
const props=defineProps<{fileDrafts?:Record<string,File[]>;pendingMessages?:PendingMessage[];retryPending?:(p:PendingMessage)=>Promise<void>;dismissPending?:(id:string)=>void;editMessage?:(message:Message,text:string,mentions:Mention[])=>Promise<void>;canWrite?:boolean;loadFile?:(file:NonNullable<Message['attachment']>)=>Promise<Blob>;request:(path:string)=>Promise<any>;downloadFile:(file:NonNullable<Message['attachment']>)=>Promise<void>;avatars?:Record<string,string>;active?:boolean;unreadMentions?:string[];members?:Member[];roles?:Role[];focusMessage?:string;sendMessage:(replyTo?:string,mentions?:Mention[],files?:File[])=>Promise<boolean>;canModerate:boolean;mutedUntil:number;removeMessage:(message:Message)=>Promise<void>;messages:Message[];modelValue:string;channel:string;selfId:string;hasMore:boolean;loadingOlder:boolean;sending:boolean;reactionBusy:Record<string,boolean>;loadOlder:()=>Promise<void>}>();
function writingAllowed(){return props.canWrite!==false;}
const emit=defineEmits<{'read-mentions':[ids:string[]];'update:modelValue':[value:string];react:[message:Message,emoji:string];'member-menu':[message:Message,event:MouseEvent]}>();
function attachments(message:Message){return message.deleted?[]:message.attachments||(message.attachment?[message.attachment]:[]);}
function visualFile(name:string){return ['image','video'].includes(attachmentMedia(name)?.kind||'');}
function visualAttachments(message:Message){return attachments(message).filter(f=>visualFile(f.name));}
function otherAttachments(message:Message){return attachments(message).filter(f=>!visualFile(f.name));}
function visualCount(message:Message){return attachments(message).filter(f=>['image','video'].includes(attachmentMedia(f.name)?.kind||'')).length;}
const fileInput=ref<HTMLInputElement>(),fileError=ref(''),dragging=ref(false),panel=ref<HTMLElement>();
const fileDrafts=ref<Record<string,File[]>>(props.fileDrafts||{}),downloading=ref('');
const attached=computed(()=>fileDrafts.value[props.channel]||[]);let policyVersion=0;
function fileSize(bytes:number){return bytes<1024?`${bytes} Б`:bytes<1024*1024?`${(bytes/1024).toFixed(1)} КБ`:`${(bytes/1024/1024).toFixed(2)} МБ`;}
watch(()=>[props.channel,props.active],()=>{policyVersion++;dragging.value=false;fileError.value='';});
async function attachFiles(files:File[]){
 if(!writingAllowed()||!files.length||!props.active||props.mutedUntil||props.sending)return;
 const error=fileSelectionError([...(attached.value),...files]);if(error){fileError.value=error;return;}
 const channel=props.channel,version=++policyVersion;fileError.value='';
 try{
  const result=await props.request('/api/channels/'+encodeURIComponent(channel)+'/files');
  if(!writingAllowed()||version!==policyVersion||channel!==props.channel||!props.active||props.mutedUntil||props.sending)return;
  if(!result.policy.enabled){fileError.value='Файлы в этом канале выключены. Владелец может включить их в настройках каналов.';return;}
  const combined=[...(fileDrafts.value[channel]||[]),...files];
  const error=fileSelectionError(combined);if(error){fileError.value=error;return;}
  fileDrafts.value[channel]=combined;
 }catch{if(version===policyVersion)fileError.value='Не удалось проверить доступ к файлам. Попробуйте прикрепить файл ещё раз.';}
}
function chooseFile(e:Event){const el=e.target as HTMLInputElement,files=Array.from(el.files||[]);el.value='';void attachFiles(files);}
function fileDrag(e:DragEvent){
 if(!props.active||!preventFileNavigation(e))return;
 const inside=!!panel.value?.contains(e.target as Node);
 dragging.value=props.canWrite!==false&&inside&&!props.mutedUntil&&!props.sending;
 if(e.dataTransfer)e.dataTransfer.dropEffect=dragging.value?'copy':'none';
}
function fileDrop(e:DragEvent){
 if(!props.active||!preventFileNavigation(e))return;
 dragging.value=false;
 if(panel.value?.contains(e.target as Node))void attachFiles(Array.from(e.dataTransfer?.files||[]));
}
function fileLeave(e:DragEvent){if(!e.relatedTarget)dragging.value=false;}
onMounted(()=>{window.addEventListener('dragover',fileDrag);window.addEventListener('drop',fileDrop);window.addEventListener('dragleave',fileLeave);});
onBeforeUnmount(()=>{policyVersion++;window.removeEventListener('dragover',fileDrag);window.removeEventListener('drop',fileDrop);window.removeEventListener('dragleave',fileLeave);});
async function download(file:NonNullable<Message['attachment']>){if(downloading.value)return;downloading.value=file.id;fileError.value='';try{await props.downloadFile(file);}catch(e){fileError.value=(e as Error).message;}finally{downloading.value='';}}
const replying=ref<Message>(),deleting=ref<Message>(),deleteBusy=ref(false),deleteError=ref('');
const replyTarget=computed(()=>props.messages.find(m=>m.id===replying.value?.id));
watch(()=>props.messages,()=>{if(replying.value&&!props.messages.some(m=>m.id===replying.value!.id))replying.value=undefined;});
function quote(message:Message){const live=props.messages.find(m=>m.id===message.replyTo);return live?{id:live.id,name:live.name,text:live.text,deleted:live.deleted}:message.reply;}
async function reply(message:Message){replying.value=message;await nextTick();input.value?.focus();}
async function send(){
 if(!writingAllowed()||props.mutedUntil||props.sending)return;
 const target=replying.value,channel=props.channel,text=props.modelValue,files=attached.value;
 const result=props.sendMessage(target?.id,withEveryone(text,currentMentions()),files);
 // Uploads move into an in-memory outbox immediately; retries keep the same ID.
 if(files.length){delete fileDrafts.value[channel];emit('update:modelValue','');replying.value=undefined;mentionDrafts.delete(channel);mentionStart.value=-1;}
 if(await result){if(props.channel===channel&&replying.value===target){replying.value=undefined;if(!props.modelValue||props.modelValue===text){mentionDrafts.delete(channel);mentionStart.value=-1;}}}
}
const editing=ref<Message>(),editText=ref(''),editBusy=ref(false),editError=ref(''),editInput=ref<HTMLTextAreaElement[]>();
async function beginEdit(message:Message){editing.value={...message};editText.value=message.text;editError.value='';await nextTick();editInput.value?.[0]?.focus();}
async function saveEdit(){if(!editing.value||!props.editMessage||editBusy.value)return;editBusy.value=true;editError.value='';try{const m=editing.value;await props.editMessage(m,editText.value,moveMentions(m.text,editText.value,m.mentions||[]));editing.value=undefined;}catch(e){editError.value=(e as Error).message;}finally{editBusy.value=false;}}
watch(()=>props.channel,()=>{editing.value=undefined;});
watch(()=>props.pendingMessages?.length,async()=>{await nextTick();if(list.value)list.value.scrollTop=list.value.scrollHeight;});

async function confirmDelete(){if(!deleting.value)return;deleteBusy.value=true;deleteError.value='';try{await props.removeMessage(deleting.value);deleting.value=undefined;}catch(e){deleteError.value=(e as Error).message;}finally{deleteBusy.value=false;}}
watch(()=>props.channel,()=>{replying.value=undefined;deleting.value=undefined;});
const input=ref<HTMLTextAreaElement>(),list=ref<HTMLElement>(),loading=ref(false);let observer:ResizeObserver|undefined;
useVisibleMentions(list,()=>({active:!!props.active,ids:props.unreadMentions||[],channel:props.channel,count:props.messages.length}),ids=>emit('read-mentions',ids));
function resize(){const el=input.value;if(!el||!el.clientWidth)return;el.style.height='0px';const max=72;el.style.height=Math.min(max,el.scrollHeight)+'px';el.style.overflowY=el.scrollHeight>max?'auto':'hidden';}
watch(()=>props.modelValue,()=>nextTick(resize));
watch(()=>props.channel,async()=>{await nextTick();resize();if(list.value)list.value.scrollTop=list.value.scrollHeight;});
watch(()=>props.messages.at(-1)?.id,async()=>{const el=list.value;const near=el&&el.scrollHeight-el.scrollTop-el.clientHeight<100;await nextTick();if(el&&near&&!loading.value)el.scrollTop=el.scrollHeight;});
onMounted(()=>{observer=new ResizeObserver(resize);if(input.value)observer.observe(input.value);resize();});onBeforeUnmount(()=>observer?.disconnect());
async function older(){if(!list.value)return;const el=list.value,height=el.scrollHeight,top=el.scrollTop;loading.value=true;await props.loadOlder();await nextTick();el.scrollTop=top+el.scrollHeight-height;loading.value=false;}
async function insert(emoji:string){const el=input.value;const start=el?.selectionStart??props.modelValue.length,end=el?.selectionEnd??start;const value=props.modelValue.slice(0,start)+emoji+props.modelValue.slice(end);if(value.length>8000)return;emit('update:modelValue',value);await nextTick();el?.focus();el?.setSelectionRange(start+emoji.length,start+emoji.length);}
const mentionDrafts=new Map<string,{text:string;mentions:Mention[]}>();
const mentionStart=ref(-1),mentionQuery=ref(''),activeChoice=ref(0);
const choices=computed(()=>mentionChoices(props.members||[],props.roles||[],props.selfId).filter(c=>c.label.toLocaleLowerCase().includes(mentionQuery.value.toLocaleLowerCase())).slice(0,40));
function currentMentions(){const old=mentionDrafts.get(props.channel);if(!old)return [];return moveMentions(old.text,props.modelValue,old.mentions);}
watch(()=>[props.channel,props.modelValue],()=>{const mentions=currentMentions();mentionDrafts.set(props.channel,{text:props.modelValue,mentions});});
watch(()=>props.channel,()=>mentionStart.value=-1);
function findMention(){
 const el=input.value;if(!el||el.selectionStart!==el.selectionEnd){mentionStart.value=-1;return;}
 const prefix=el.value.slice(0,el.selectionStart),match=prefix.match(/(?:^|\s)@([^@\n]{0,64})$/);
 const start=match?el.selectionStart-match[1]!.length-1:-1;
 if(start<0||currentMentions().some(m=>start>=m.start&&start<m.end)){mentionStart.value=-1;return;}
 mentionStart.value=start;mentionQuery.value=match![1]!;activeChoice.value=0;
}
function updateText(e:Event){emit('update:modelValue',(e.target as HTMLTextAreaElement).value);findMention();}
async function selectMention(choice:MentionChoice){
 const el=input.value,start=mentionStart.value;if(!el||start<0)return;
 const end=el.selectionStart,tag='@'+choice.label,value=props.modelValue.slice(0,start)+tag+' '+props.modelValue.slice(end);
 if(value.length>8000)return;
 const mentions=moveMentions(props.modelValue,value,currentMentions());mentions.push({kind:choice.kind,id:choice.id,label:choice.label,start,end:start+tag.length});mentions.sort((a,b)=>a.start-b.start);
 mentionDrafts.set(props.channel,{text:value,mentions});emit('update:modelValue',value);mentionStart.value=-1;
 await nextTick();el.focus();el.setSelectionRange(start+tag.length+1,start+tag.length+1);
}
function keydown(e:KeyboardEvent){
 if(e.isComposing||e.keyCode===229)return;
 if(mentionStart.value>=0){
  if(e.key==='Escape'){e.preventDefault();e.stopPropagation();mentionStart.value=-1;return;}
  if(choices.value.length&&(e.key==='ArrowDown'||e.key==='ArrowUp')){e.preventDefault();activeChoice.value=(activeChoice.value+(e.key==='ArrowDown'?1:-1)+choices.value.length)%choices.value.length;void nextTick(()=>document.getElementById('mention-option-'+activeChoice.value)?.scrollIntoView({block:'nearest'}));return;}
  if(choices.value.length&&(e.key==='Enter'&&!e.shiftKey||e.key==='Tab')){e.preventDefault();void selectMention(choices.value[activeChoice.value]!);return;}
 }
 if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();mentionStart.value=-1;void send();}
}
watch(()=>[props.focusMessage,props.messages.length],async()=>{if(!props.focusMessage)return;await nextTick();const target=list.value?.querySelector<HTMLElement>('[data-message-id="'+CSS.escape(props.focusMessage)+'"]');target?.scrollIntoView({block:'center'});});
function time(value:number){return new Intl.DateTimeFormat('ru',{day:'numeric',month:'short',hour:'2-digit',minute:'2-digit'}).format(value);}
</script>
<template>
 <section ref="panel" class="chat-panel" aria-label="Текстовый чат">
  <div v-if="dragging" class="file-drop-overlay"><AppIcon name="paperclip" :size="28"/><strong>Прикрепить файл</strong><span>До 10 файлов · всего до 20 МБ · отправятся вместе с сообщением</span></div>
  <div ref="list" class="chat-messages" role="log" aria-label="Сообщения" aria-live="polite" aria-relevant="additions text">
   <button v-if="hasMore" class="text-button older-messages" :disabled="loadingOlder" @click="older">{{loadingOlder?'Загрузка…':'Показать предыдущие сообщения'}}</button>
   <article v-for="message in messages" :key="message.id" class="chat-message" :class="{'message-target':message.id===focusMessage}" :data-message-id="message.id" tabindex="0" @contextmenu.prevent="emit('member-menu',message,$event)"><span class="avatar small"><img v-if="avatars?.[message.authorId]" :src="avatars[message.authorId]" alt=""><template v-else>{{message.name?.slice(0,2).toUpperCase()}}</template></span><div class="message-body"><div class="message-meta"><strong>{{message.name}}</strong><time :datetime="new Date(message.createdAt).toISOString()">{{time(message.createdAt)}}</time><small v-if="message.editedAt" :title="time(message.editedAt)">изменено</small></div><div v-if="message.replyTo&&!message.deleted" class="quoted-message"><strong>{{quote(message)?.name}}</strong><span>{{quote(message)?.deleted?'Сообщение удалено':quote(message)?.text}}</span></div><form v-if="editing?.id===message.id" class="message-edit" @submit.prevent="saveEdit"><textarea ref="editInput" v-model="editText" maxlength="8000" aria-label="Редактировать сообщение" :disabled="editBusy" @keydown.esc.stop.prevent="editing=undefined" @keydown.ctrl.enter.prevent="saveEdit"/><div><button class="soft-button" :disabled="editBusy" type="submit">Сохранить</button><button class="text-button" type="button" :disabled="editBusy" @click="editing=undefined">Отмена</button><small>Ctrl + Enter — сохранить</small></div><small v-if="editError" role="alert">{{editError}}</small></form><p v-else :class="{'deleted-message':message.deleted}"><template v-if="message.deleted">Сообщение удалено</template><template v-else><span v-for="(part,i) in messageParts(message.text,message.mentions,members?.find(m=>m.id===selfId))" :key="i" :title="part.personal?'Упоминание для вас':undefined" :class="{'message-mention':part.mention,'personal-mention':part.personal}"><template v-if="part.mention">{{part.text}}</template><LinkedText v-else :text="part.text"/></span></template></p><div v-if="visualAttachments(message).length" class="message-attachments"><AttachmentPreview v-for="(file,index) in visualAttachments(message)" :gallery="visualAttachments(message).map(f=>({name:f.name,src:remoteServer()?'':'/api/session/media/'+encodeURIComponent(f.id),load:loadFile?()=>loadFile!(f):undefined}))" :gallery-index="index" :compact="visualCount(message)>1" :key="file.id" :src="remoteServer()?'':'/api/session/media/'+encodeURIComponent(file.id)" :name="file.name" :size="file.size" :scanned="file.scanned" :load="loadFile?()=>loadFile!(file):undefined" :downloading="downloading===file.id" @download="download($event===undefined?file:visualAttachments(message)[$event]!)"/></div><div v-if="otherAttachments(message).length" class="other-attachments"><AttachmentPreview v-for="file in otherAttachments(message)" :key="file.id" :src="remoteServer()?'':'/api/session/media/'+encodeURIComponent(file.id)" :name="file.name" :size="file.size" :scanned="file.scanned" :load="loadFile?()=>loadFile!(file):undefined" :downloading="downloading===file.id" @download="download(file)"/></div><div v-if="Object.keys(message.reactions||{}).length" class="message-reactions"><ReactionButton v-for="(users,emoji) in message.reactions" :key="emoji" :emoji="emoji" :users="users" :members="members||[]" :self-id="selfId" :disabled="!!mutedUntil||reactionBusy[message.id+emoji]" @toggle="emit('react',message,emoji)"/></div><div v-if="!message.deleted" class="message-actions"><EmojiPicker v-if="!mutedUntil" icon="reaction-solid" label="Добавить реакцию" @select="emit('react',message,$event)"/><button v-if="!mutedUntil&&canWrite!==false" type="button" class="icon-button" aria-label="Ответить на сообщение" title="Ответить" @click="reply(message)"><AppIcon name="reply-solid" :size="18"/></button><button v-if="editMessage&&message.authorId===selfId&&!mutedUntil&&canWrite!==false" class="icon-button" type="button" aria-label="Редактировать сообщение" title="Редактировать" @click="beginEdit(message)"><svg width="17" height="17" viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="m15 5 4 4M4 20l4-1L20 7a2.8 2.8 0 0 0-4-4L4 15v5Z" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round"/></svg></button><button v-if="canModerate||message.authorId===selfId" type="button" class="icon-button" :aria-label="'Удалить сообщение от '+message.name" title="Удалить" @click="deleting=message"><AppIcon name="trash-solid" :size="18"/></button></div></div></article>
   <article v-for="pending in (pendingMessages||[]).filter(p=>!messages.some(m=>m.clientId===p.id))" :key="pending.id" class="chat-message pending-message"><span class="avatar small"><img v-if="avatars?.[selfId]" :src="avatars[selfId]" alt=""><template v-else>{{members?.find(m=>m.id===selfId)?.name.slice(0,2)}}</template></span><div class="message-body"><div class="message-meta"><strong>{{members?.find(m=>m.id===selfId)?.name}}</strong><small>Видно только вам</small></div><p v-if="pending.text">{{pending.text}}</p><div class="pending-files"><div v-for="(file,index) in pending.files" :key="index" class="pending-file"><AppIcon name="paperclip" :size="18"/><span>{{file.name}}<small>{{fileSize(file.size)}}</small></span></div></div><div class="pending-status" role="status"><span>{{pending.status==='failed'?'Не отправлено':pending.status==='checking'?'На проверке…':'Загрузка · '+pending.progress+'%'}}</span><template v-if="pending.status==='failed'"><button class="text-button" :disabled="sending||canWrite===false||!!mutedUntil" @click="retryPending?.(pending)">Повторить</button><button class="text-button" @click="dismissPending?.(pending.id)">Убрать</button><small>{{pending.error}}</small></template></div></div></article>
  </div>
  <div v-if="deleting" class="chat-delete-confirm" role="alert"><span>Удалить это сообщение?</span><button class="danger-button" :disabled="deleteBusy" @click="confirmDelete">Удалить</button><button class="text-button" :disabled="deleteBusy" @click="deleting=undefined">Отмена</button><span v-if="deleteError">{{deleteError}}</span></div><p v-if="canWrite===false" class="field-help" role="status">В этом канале могут писать только выбранные роли.</p><p v-if="mutedUntil" class="field-help" role="status">Отправка сообщений ограничена до {{time(mutedUntil)}}.</p><div v-if="replyTarget" class="reply-draft"><div><strong>Ответ: {{replyTarget.name}}</strong><span>{{replyTarget.deleted?'Сообщение удалено':replyTarget.text}}</span></div><button class="icon-button" aria-label="Отменить ответ" @click="replying=undefined"><AppIcon name="close" :size="18"/></button></div>
  <p v-if="fileError" class="field-help" role="alert">{{fileError}}</p><div v-if="attached.length" class="file-draft-wrap"><div v-for="(file,index) in attached" :key="index" class="file-draft"><AttachmentPreview :file="file" :name="file.name" draft/><span class="file-draft-label" :title="file.name">{{file.name}}<small>{{fileSize(file.size)}}</small></span><button type="button" class="icon-button" :disabled="sending" :aria-label="'Убрать '+file.name" @click="fileDrafts[channel]=attached.filter((_,i)=>i!==index)"><AppIcon name="close" :size="16"/></button></div></div><div class="compose-wrap"><div v-if="mentionStart>=0" id="mention-options" class="mention-options" role="listbox" aria-label="Кого упомянуть"><div class="mention-heading">Упомянуть</div><button v-for="(choice,i) in choices" :id="'mention-option-'+i" :key="choice.kind+choice.id" type="button" role="option" :aria-selected="i===activeChoice" :class="{active:i===activeChoice}" @pointerdown.prevent @click="selectMention(choice)"><span class="mention-avatar">{{choice.kind==='member'?choice.label.slice(0,2).toUpperCase():'@'}}</span><span><strong>{{choice.label}}</strong><small>{{choice.detail}}</small></span></button><p v-if="!choices.length">Никого не найдено</p></div>
  <form class="chat-compose" @submit.prevent="send"><input ref="fileInput" style="display:none" type="file" multiple tabindex="-1" aria-hidden="true" @change="chooseFile"><button type="button" class="icon-button" :disabled="canWrite===false||!!mutedUntil||sending" aria-label="Прикрепить файл до 20 МБ" title="Прикрепить файл до 20 МБ" @click="fileInput?.click()"><AppIcon name="paperclip" :size="20"/></button><label class="sr-only" for="message-input">Сообщение</label><textarea id="message-input" ref="input" :disabled="canWrite===false||!!mutedUntil" :value="modelValue" maxlength="8000" rows="1" :placeholder="canWrite===false?'Канал только для чтения':'Написать… @ — упомянуть'" :aria-expanded="mentionStart>=0" aria-autocomplete="list" :aria-controls="mentionStart>=0?'mention-options':undefined" :aria-activedescendant="mentionStart>=0&&choices.length?'mention-option-'+activeChoice:undefined" @input="updateText" @click="findMention" @keyup.left="findMention" @keyup.right="findMention" @blur="mentionStart=-1" @keydown="keydown"></textarea><span v-if="modelValue.length>=7800" class="chat-character-count" :class="{limit:modelValue.length>=8000}" :aria-label="`Введено ${modelValue.length} из 8000 символов`">{{modelValue.length}} / 8000</span><EmojiPicker v-if="!mutedUntil&&canWrite!==false" @select="insert"/><button class="primary chat-send" :disabled="canWrite===false||!!mutedUntil||sending||(!modelValue.trim()&&!attached.length)" aria-label="Отправить сообщение"><AppIcon name="arrow" :size="20"/></button></form></div>
 </section>
</template>
<style scoped>
.chat-panel{position:relative}.file-drop-overlay{position:absolute;inset:0;z-index:30;pointer-events:none;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:12px;border:1px dashed #78a7ce;border-radius:14px;background:#151c24ed;color:#d5e9fc}.file-drop-overlay span{font-size:12px;color:#aebfce}.file-draft{flex:none}
.deleted-message{color:var(--quiet);font-style:italic}.chat-delete-confirm{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.message-actions{display:flex;gap:3px;height:30px;margin-top:4px;opacity:0;pointer-events:none}.message-actions button{height:30px;width:30px;min-height:30px;min-width:30px;padding:5px;background:transparent}.chat-message:hover .message-actions,.chat-message:focus-within .message-actions,.message-actions:has([aria-expanded=true]){opacity:1;pointer-events:auto}.quoted-message{border-left:2px solid #657b74;padding:5px 10px;margin:5px 0 8px;font-size:11px;color:#aaa}.quoted-message span,.reply-draft span{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;max-width:100%}.reply-draft{display:flex;align-items:center;justify-content:space-between;gap:8px;background:#191c1b;padding:10px 14px;border-radius:12px;font-size:12px;flex:none}.reply-draft>div{min-width:0}.reply-draft span{color:#888;margin-top:4px}@media(hover:none){.message-actions{opacity:1;pointer-events:auto}}

.chat-panel{display:flex;flex-direction:column;flex:1;min-height:0;gap:16px}.chat-messages{height:auto;flex:1;min-height:0;overflow:auto;padding:0 4px 0 0;scrollbar-width:thin;scrollbar-color:#484848 transparent}.chat-compose{margin:0;flex:none;display:flex;align-items:flex-end;gap:8px;background:#171717;border-radius:16px;padding:12px;box-shadow:none;border:0}.chat-compose textarea{flex:1;width:0;min-height:24px;height:24px;max-height:72px;line-height:24px;padding:0 6px;margin:6px 0;background:transparent;color:var(--text);border:0;outline:none!important;box-shadow:none!important;resize:none;border-radius:0;scrollbar-width:thin;scrollbar-color:#555 transparent}.chat-compose:focus-within{background:#1c1c1c}.chat-compose textarea::-webkit-scrollbar,.chat-messages::-webkit-scrollbar{width:4px}.chat-compose textarea::-webkit-scrollbar-thumb,.chat-messages::-webkit-scrollbar-thumb{border-radius:4px;background:#555}.chat-compose textarea::-webkit-scrollbar-button,.chat-messages::-webkit-scrollbar-button{display:none;width:0;height:0}.chat-send{width:36px;height:36px;padding:0;display:grid;place-items:center;border-radius:10px}.message-body{min-width:0;flex:1}.message-body p{white-space:pre-wrap;overflow-wrap:anywhere;margin-top:4px}.message-meta{min-height:28px;display:flex;align-items:center;gap:12px}.message-meta time{font-size:10px;color:var(--quiet)}.message-add-reaction{margin-left:auto;opacity:0}.chat-message:hover .message-add-reaction,.chat-message:focus-within .message-add-reaction,.message-add-reaction[aria-expanded=true]{opacity:1}.message-reactions{display:flex;gap:6px;flex-wrap:wrap;margin-top:8px}.older-messages{display:block;margin:0 auto 16px}.chat-message{margin-bottom:4px;padding:10px 6px}@media(hover:none){.message-add-reaction{opacity:1}}
@supports selector(::-webkit-scrollbar){.chat-compose textarea,.chat-messages{scrollbar-width:auto;scrollbar-color:auto}}
@property --chat-scroll-thumb{syntax:"<color>";inherits:true;initial-value:transparent}
.chat-messages{--chat-scroll-thumb:transparent;scrollbar-gutter:stable;scrollbar-color:var(--chat-scroll-thumb) transparent;transition:--chat-scroll-thumb 120ms ease}
.chat-panel:hover .chat-messages{--chat-scroll-thumb:#555}
.chat-messages::-webkit-scrollbar-thumb{background:var(--chat-scroll-thumb)}
@supports selector(::-webkit-scrollbar){.chat-messages{scrollbar-color:auto}}
@media(hover:none){.chat-messages{--chat-scroll-thumb:#555}}
@media(prefers-reduced-motion:reduce){.chat-messages{transition:none}}
.chat-messages{margin-left:var(--room-content-inset,0px)}
.chat-message{position:relative;border-radius:12px;min-height:62px}.chat-message:hover,.chat-message:focus-within{background:#111}.message-actions{position:absolute;right:8px;top:5px;display:flex;align-items:center;gap:2px;margin:0;height:34px;padding:2px 3px;background:#262626;border-radius:10px;box-shadow:0 4px 12px #0004}.message-actions button{display:grid;place-items:center;margin:0;padding:0;width:30px;height:30px;min-height:30px;color:#b8b8b8;border-radius:8px}.message-actions button:hover{background:#3b3b3b;color:#fff}.message-actions :deep(svg){display:block;width:18px;height:18px}.message-meta{padding-right:108px}.message-body p{padding-right:10px}.message-reactions{margin-bottom:2px}@media(max-width:600px){.message-meta{padding-right:0;flex-wrap:wrap;gap:4px 10px}.message-actions{top:-13px;right:4px}}
</style>
<style scoped>
.mention-options button{justify-content:flex-start}.mention-options{scrollbar-color:#555 transparent}.mention-options::-webkit-scrollbar{width:4px}.mention-options::-webkit-scrollbar-thumb{background:#555;border-radius:4px}.mention-options::-webkit-scrollbar-button{display:none;width:0;height:0}
</style>
<style scoped>
.compose-wrap{position:relative;flex:none;min-width:0}.mention-options{position:absolute;bottom:calc(100% + 8px);left:0;width:min(360px,100%);max-height:min(320px,50dvh);overflow:auto;scrollbar-width:thin;padding:8px;background:#252525;border-radius:14px;box-shadow:0 12px 40px #0008;z-index:20}.mention-heading{padding:8px 10px;color:#999;font-size:11px}.mention-options button{display:flex;align-items:center;gap:10px;width:100%;padding:8px 10px;text-align:left;background:transparent;border-radius:10px;color:#ddd}.mention-options button.active,.mention-options button:hover{background:#383838}.mention-avatar{display:grid;place-items:center;flex:none;width:32px;height:32px;background:#444;border-radius:10px;font-size:12px}.mention-options button>span:last-child{min-width:0}.mention-options strong{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:12px}.mention-options small{display:block;margin-top:3px;color:#aaa;font-size:10px}.mention-options p{padding:8px 10px;color:#aaa;font-size:12px}.message-mention{color:#c3e5df;background:#2d403d;border-radius:4px;padding:1px 2px}.chat-message.message-target{background:#1c2825}
/* Composer actions share the same box, independent of global button rules. */
.chat-compose{position:relative;border-radius:14px;align-items:flex-end;padding:10px 12px;gap:8px}
.chat-compose :deep(button){display:grid;place-items:center;flex:0 0 36px;width:36px;height:36px;min-width:36px;min-height:36px;margin:0;padding:0;border-radius:10px}
.chat-compose :deep(button svg){display:block;width:20px;height:20px}
.chat-compose :deep(.emoji-trigger:hover),.chat-compose :deep(.emoji-trigger:focus-visible),.chat-compose :deep(.emoji-trigger[aria-expanded=true]){background:#303030;color:#fff}
</style>
<style scoped>.message-mention.personal-mention{background:#285b9d;color:#f1f7ff;font-weight:500}</style>

<style scoped>
.chat-character-count{position:absolute;right:12px;bottom:56px;width:80px;text-align:center;pointer-events:none;font-size:10px;line-height:14px;color:var(--muted);font-variant-numeric:tabular-nums;white-space:nowrap}.chat-character-count.limit{color:#efaaaa}
</style>

<style scoped>.chat-file{display:flex;gap:12px;align-items:center;max-width:100%;text-align:left;background:#242424;padding:12px;border-radius:12px;margin:8px 0}.chat-file>span{min-width:0;overflow-wrap:anywhere}.chat-file small{display:block;font-size:10px;color:#999;margin-top:4px}.chat-file>span:last-child{color:#8fc4f5;font-size:11px}.file-draft{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:8px 14px;border-radius:12px;background:#202020;font-size:12px}.file-draft>span{overflow-wrap:anywhere;min-width:0}</style>
<style scoped>.file-draft-wrap{flex:none;display:flex;gap:8px;overflow-x:auto;padding:0 0 8px;max-height:96px}.file-draft-wrap .file-draft{flex:0 0 300px;max-width:100%;height:76px;padding:8px;box-sizing:border-box;margin:0;gap:10px}.file-draft-label{flex:1;white-space:nowrap;text-overflow:ellipsis;overflow:hidden}.file-draft-label small{display:block;color:#999;font-size:11px;margin-top:5px}.file-draft .icon-button{flex:none}</style>
<style scoped>.message-attachments{display:flex;flex-wrap:wrap;align-items:flex-start;gap:8px;margin:8px 0;max-width:100%;min-width:0}.other-attachments{display:flex;flex-direction:column;gap:8px;margin:8px 0}.other-attachments>*{margin:0}.message-attachments>:not(.compact){flex:0 1 auto;min-width:0;margin:0}</style>

<style scoped>.message-edit{margin:10px 0}.message-edit textarea{display:block;width:100%;min-height:90px;max-height:300px;resize:vertical;margin:0 0 8px;line-height:1.6}.message-edit>div{display:flex;align-items:center;gap:8px}.message-edit small,.pending-status,.message-meta>small{font-size:11px;color:#999}.pending-files{display:flex;flex-wrap:wrap;gap:6px;margin-top:10px}.pending-file{display:flex;gap:10px;align-items:center;background:#222;padding:12px;border-radius:12px;max-width:400px;min-width:0}.pending-file span{overflow-wrap:anywhere}.pending-file small{display:block;font-size:10px;color:#999;margin-top:4px}.pending-status{display:flex;flex-wrap:wrap;align-items:center;gap:8px;margin-top:8px}.pending-status small{flex-basis:100%;color:#d6aaaa}.pending-status button{font-size:11px;padding:5px}.pending-message{opacity:.85}</style>
