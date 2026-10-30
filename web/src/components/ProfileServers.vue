<script setup lang="ts">
import {computed,ref} from 'vue';
import {enrollServer} from '../composables/enrollServer';
import AppIcon from './AppIcon.vue';
import {serverAddress,newServerKey,serverProfileUrl,type SavedServer} from '../composables/profileFile';
const props=defineProps<{servers:SavedServer[];name:string;currentRole?:SavedServer['role'];save:(servers:SavedServer[])=>void;leave:(url:string)=>Promise<void>}>();
const currentServer=location.origin,editing=ref<string|null>(null),draftName=ref(''),draftUrl=ref(''),note=ref(''),removing=ref(''),leaving=ref(false),saving=ref(false),inviteCode=ref('');
let enrollmentUrl='',enrollmentKey='';
const sorted=computed(()=>props.servers.map((s,i)=>({s,i})).sort((a,b)=>Number(!!b.s.pinned)-Number(!!a.s.pinned)||a.i-b.i).map(v=>v.s));
function edit(server?:SavedServer){if(saving.value)return;inviteCode.value='';editing.value=server?.url||'';draftName.value=server?.name||'';draftUrl.value=server?.url||'';note.value='';removing.value='';}
function commit(servers:SavedServer[]){try{props.save(servers);note.value='';return true;}catch(e){note.value=(e as Error).message;return false;}}
async function save(){
 if(saving.value)return;saving.value=true;note.value='';
 try{
  const url=serverAddress(draftUrl.value),before=props.servers.find(s=>s.url===editing.value);
  if(before){
   const name=draftName.value.trim();if(!name||[...name].length>60||/[\u0000-\u001f\u007f]/.test(name))throw new Error('Название должно содержать от 1 до 60 символов.');
   if(commit(props.servers.map(s=>s.url===before.url?{...s,name,customName:true}:s)))editing.value=null;
   return;
  }
  if(props.servers.some(s=>s.url===url))throw new Error('Этот сервер уже есть в списке.');
  if(props.servers.length>=64)throw new Error('Можно сохранить до 64 серверов.');
  if(!props.name.trim())throw new Error('Сначала укажите своё имя в профиле.');
  if(!inviteCode.value.trim())throw new Error('Введите код приглашения.');
  if(enrollmentUrl!==url){enrollmentUrl=url;enrollmentKey=newServerKey();}
  const result=await enrollServer(url,props.name,inviteCode.value.trim(),enrollmentKey);
  if(typeof result?.serverName!=='string'||!result.serverName.trim()||[...result.serverName].length>60||result.profileKey!==enrollmentKey||typeof result.member?.name!=='string')throw new Error('Сервер вернул некорректный ответ.');
  const entry:SavedServer={url,name:result.serverName,displayName:result.member.name,profileKey:enrollmentKey,...(result.member.role&&['owner','admin','member'].includes(result.member.role)?{role:result.member.role}:{})};
  if(commit([...props.servers,entry])){editing.value=null;inviteCode.value='';enrollmentUrl='';enrollmentKey='';}
 }catch(e){note.value=(e as Error).message;}finally{saving.value=false;}
}
function pin(server:SavedServer){commit(props.servers.map(s=>s.url===server.url?{...s,pinned:!s.pinned}:s));}
async function remove(url:string){if(leaving.value)return;leaving.value=true;note.value='';try{await props.leave(url);removing.value='';}catch(e){note.value=(e as Error).message;}finally{leaving.value=false;}}
function role(server:SavedServer){return server.url===currentServer?props.currentRole:server.role;}
</script>
<template>
 <nav class="profile-servers" aria-labelledby="profile-servers-title">
  <h3 id="profile-servers-title">Мои серверы <span>{{servers.length}}</span></h3>
  <ul class="server-list"><li v-for="server in sorted" :key="server.url" class="saved-server" :class="{current:server.url===currentServer}">
   <a :href="serverProfileUrl(server)" :aria-current="server.url===currentServer?'page':undefined"><strong>{{server.name}}<AppIcon v-if="role(server)==='owner'" name="crown" :size="14" class="server-crown" :title="server.url===currentServer?'Вы владелец':'Владелец при последнем входе'" aria-label="Владелец"/></strong><small>{{server.url.replace(/^https?:\/\//,'')}}</small><small v-if="role(server)==='admin'" class="server-role">Администратор{{server.url===currentServer?'':' · последний вход'}}</small></a>
   <button type="button" class="server-pin" :class="{pinned:server.pinned}" :aria-pressed="!!server.pinned" :aria-label="(server.pinned?'Открепить ':'Закрепить ')+server.name" :title="server.pinned?'Открепить':'Закрепить'" @click="pin(server)"><AppIcon name="pin" :size="17"/></button>
   <div class="saved-server-actions"><button type="button" :aria-label="'Изменить '+server.name" @click="edit(server)">Изменить</button><button type="button" :aria-label="'Покинуть сервер '+server.name" @click="removing=server.url;editing=null;note=''" class="server-remove" :disabled="leaving">Покинуть</button></div>
   <div v-if="removing===server.url" class="server-remove-confirm"><p v-if="role(server)==='owner'">Сначала передайте владение сервером другому участнику.</p><template v-else-if="server.url!==currentServer"><p>Откройте этот сервер, чтобы покинуть его и удалить себя из участников.</p><a :href="serverProfileUrl(server)">Открыть сервер</a></template><p v-else>Покинуть сервер? Ваш профиль и аватар на нём удалятся. Отправленные сообщения останутся в чатах.</p><div><button v-if="role(server)!=='owner'&&server.url===currentServer" type="button" :disabled="leaving" @click="remove(server.url)">{{leaving?'Выходим…':'Покинуть сервер'}}</button><button type="button" :disabled="leaving" @click="removing=''">Отмена</button></div></div>
  </li></ul>
  <button v-if="editing===null" class="add-server" type="button" @click="edit()"><AppIcon name="plus" :size="17"/>Добавить сервер</button>
  <form v-else class="server-editor" @submit.prevent="save"><h4>{{editing?'Изменить сервер':'Новый сервер'}}</h4><label v-if="editing">Название<input v-model="draftName" required maxlength="60" autocomplete="off" :disabled="saving"></label><label>Адрес<input v-model="draftUrl" required inputmode="url" autocomplete="url" placeholder="https://nimbo.example.com" :readonly="!!editing" :disabled="saving"></label><label v-if="!editing">Код приглашения<input v-model="inviteCode" required maxlength="256" autocomplete="off" spellcheck="false" :disabled="saving"></label><div class="server-editor-actions"><button type="submit" class="soft-button" :disabled="saving">{{saving?'Подключаем…':editing?'Сохранить':'Добавить'}}</button><button type="button" class="text-button" :disabled="saving" @click="editing=null;inviteCode='';note=''">Отмена</button></div></form>
  <p v-if="note" class="field-help server-error" role="alert">{{note}}</p>
 </nav>
</template>
<style scoped>
.profile-servers{min-width:0}.profile-servers h3{font-size:12px;color:var(--muted);margin:0 12px 16px;display:flex;justify-content:space-between}.profile-servers h3 span{color:var(--quiet)}.server-list{list-style:none;margin:0;padding:0;display:grid;gap:6px;max-height:58dvh;overflow:auto;scrollbar-width:thin}.saved-server{position:relative;border-radius:12px;background:#131313;padding:4px;min-width:0}.saved-server.current{background:#232323}.saved-server a{display:block;padding:10px 8px 6px;border-radius:8px;overflow-wrap:anywhere}.saved-server a:hover{background:#2b2b2b}.saved-server strong{padding-right:30px;min-height:20px;display:flex;align-items:center;gap:6px;font-size:12px;line-height:18px}.saved-server small{display:block;font-size:10px;color:var(--quiet);margin-top:4px;line-height:15px}.server-crown{flex:none;color:#d7bc7d}.saved-server .server-role{color:#a9beb7}.server-pin{position:absolute;top:8px;right:8px;display:grid;place-items:center;width:28px;height:28px;min-height:28px;padding:0;border-radius:8px;background:transparent;color:#888;transition:background .14s,color .14s}.server-pin:hover,.server-pin:focus-visible{background:#363636;color:#eee}.server-pin.pinned{background:#30413b;color:#cbe2db}.server-pin.pinned:hover{background:#3c514a}.server-pin svg{display:block}.saved-server-actions{display:flex;gap:6px;padding:6px 8px 8px}.saved-server-actions button{flex:1;min-width:0;height:30px;min-height:30px;padding:0 8px;border-radius:8px;background:#252525;color:#aaa;font-size:11px;line-height:16px;transition:background .14s,color .14s}.saved-server.current .saved-server-actions button{background:#303030}.saved-server-actions button:hover,.saved-server-actions button:focus-visible{background:#3b3b3b;color:#eee}.saved-server-actions .server-remove:hover,.saved-server-actions .server-remove:focus-visible{background:#3b2a2a;color:#efaaaa}.add-server{display:flex;align-items:center;justify-content:center;gap:6px;width:100%;min-height:44px;margin-top:12px;padding:10px 6px;background:transparent!important;border:1px dashed #454545!important;border-radius:12px;color:#aaa;font-size:11px}.add-server:hover,.add-server:focus-visible{background:#202020!important;color:#eee;border-color:#777!important}.server-editor{margin-top:12px;padding:12px;background:#1c1c1c;border-radius:12px}.server-editor h4{margin:0 0 12px;font-size:12px}.server-editor label{display:block;font-size:11px;margin-bottom:10px}.server-editor input{width:100%;margin-top:6px;min-width:0;padding:10px;font-size:11px}.server-editor-actions,.server-remove-confirm>div{display:flex;gap:6px;flex-wrap:wrap}.server-editor-actions button,.server-remove-confirm button{padding:8px 10px;font-size:11px}.server-editor-actions .text-button{padding:8px 4px}.server-remove-confirm{padding:0 8px 8px}.server-remove-confirm p,.server-error{font-size:11px;line-height:1.5}.server-remove-confirm button:first-child,.server-error{color:#ef9292}@media(max-width:650px){.server-list{max-height:240px}}
</style>
