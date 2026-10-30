<script setup lang="ts">
import AvatarEditor from './AvatarEditor.vue';
import ProfileServers from './ProfileServers.vue';
import ProfileLibrary from './ProfileLibrary.vue';
import type {LocalProfile} from '../composables/profileLibrary';
import {ref,watch} from 'vue';import {type ProfileFile,type SavedServer} from '../composables/profileFile';
const props=defineProps<{name:string;nameLabel?:string;avatar:string;servers:ProfileFile['servers'];currentRole?:'owner'|'admin'|'member';saveServers:(servers:ProfileFile['servers'])=>void;leaveServer:(url:string)=>Promise<void>;profiles:LocalProfile[];activeProfileId:string;addProfile:(profile:ProfileFile)=>string;switchProfile:(id:string,code:string)=>Promise<void>;removeProfile:(id:string)=>Promise<void>;save:(name:string,avatar:string)=>Promise<void>;exportFile:(password:string)=>Promise<void>}>();
const emit=defineEmits<{logout:[];openServer:[server:SavedServer]}>();const draftName=ref(props.name),draftAvatar=ref(props.avatar),busy=ref(false),note=ref('');
watch(()=>props.name,v=>draftName.value=v);watch(()=>props.avatar,v=>draftAvatar.value=v);
async function save(){busy.value=true;note.value='';try{await props.save(draftName.value.trim(),draftAvatar.value);note.value='Профиль сохранён.';}catch(e){note.value=(e as Error).message;}finally{busy.value=false;}}
</script>
<template>
 <section class="profile-layout">
  <ProfileServers @open="emit('openServer',$event)" :servers="servers" :name="name" :current-role="currentRole" :save="saveServers" :leave="leaveServer"/>
  <div class="profile-details">
   <form class="profile-identity" @submit.prevent="save">
    <AvatarEditor :value="draftAvatar" :fallback="draftName.slice(0,2).toUpperCase()" :disabled="busy" @change="draftAvatar=$event" @remove="draftAvatar=''"/>
    <label><span>{{nameLabel||'Имя на этом сервере'}}</span><input v-model="draftName" required maxlength="40" :disabled="busy"></label>
    <button class="primary" :disabled="busy">Сохранить профиль</button>
   </form>
   <p v-if="note" class="field-help" role="status">{{note}}</p>
   <ProfileLibrary :profiles="profiles" :active-id="activeProfileId" :export-file="exportFile" :add-profile="addProfile" :switch-profile="switchProfile" :remove-profile="removeProfile"/>
   <button class="profile-logout" @click="emit('logout')">Выйти из профиля</button>
  </div>
 </section>
</template>
<style scoped>
.profile-layout{display:grid;grid-template-columns:190px minmax(0,680px);gap:20px;align-items:start}
.profile-details{min-width:0}.profile-identity{display:grid;grid-template-columns:80px minmax(0,1fr) auto;gap:16px;align-items:center}.profile-identity label{margin:0;min-width:0;position:relative}.profile-identity label>span{position:absolute;left:0;top:-22px}.profile-identity input{display:block;height:42px;margin:0}.profile-identity .primary{height:42px;align-self:center;margin-top:0;white-space:nowrap;font-size:12px}.profile-logout{display:block;margin-left:auto;color:#ef9292;background:transparent;font-size:12px;padding:10px 0}.profile-logout:hover:not(:disabled){color:#ffb1b1;background:transparent}
@media(max-width:1150px){.profile-layout{grid-template-columns:170px minmax(0,1fr)}}
@media(max-width:650px){.profile-layout{grid-template-columns:minmax(0,1fr);gap:28px}.profile-identity{gap:12px;grid-template-columns:80px minmax(0,1fr)}.profile-identity .primary{grid-column:2;justify-self:start}}
</style>
