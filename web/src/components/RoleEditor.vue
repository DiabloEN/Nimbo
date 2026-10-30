<script setup lang="ts">
import {computed,nextTick,ref} from 'vue';
import type {Member,Role} from '../composables/useWorkspace';
import {permissionLabels,permitted} from '../composables/roles';
const props=defineProps<{roles:Role[];self?:Member;members:Member[];mutate:(path:string,method:string,body?:unknown)=>Promise<unknown>}>();
const editing=ref<string>(),name=ref(''),permissions=ref<string[]>([]),busy=ref(false),error=ref(''),removal=ref<Role>(),root=ref<HTMLElement>();
const displayedRoles=computed(()=>editing.value===''?[...props.roles,{id:'',name:'',permissions:[]}]:props.roles);
function canEdit(role:Role){if(role.id==='owner')return props.self?.role==='owner';return permitted(props.self,'manage_roles')&&(props.self?.role==='owner'||!role.permissions.includes('manage_roles')&&!props.self?.roleIds?.includes(role.id)&&role.permissions.every(p=>permitted(props.self,p)));}
function nameOnly(id:string){return id==='owner'||id==='member';}
async function edit(role?:Role){if(busy.value)return;editing.value=role?.id||'';name.value=role?.name||'';permissions.value=[...role?.permissions||[]];error.value='';removal.value=undefined;await nextTick();const input=root.value?.querySelector<HTMLInputElement>('.role-name-input');input?.focus();input?.select();}
function cancel(){if(busy.value)return;editing.value=undefined;removal.value=undefined;error.value='';}
async function run(action:()=>Promise<unknown>){if(busy.value)return;busy.value=true;error.value='';try{await action();editing.value=undefined;removal.value=undefined;}catch(e){error.value=(e as Error).message;}finally{busy.value=false;}}
async function save(){if(editing.value===undefined)return;if(!name.value.trim()){error.value='Введите название роли.';return;}const id=editing.value;await run(()=>props.mutate(id?'/api/roles/'+id:'/api/roles',id?'PATCH':'POST',id==='owner'?{name:name.value.trim()}:{name:name.value.trim(),permissions:permissions.value}));}
function confirmRemoval(role:Role){cancel();removal.value=role;}
function describe(role:Role){if(role.id==='owner')return 'Все права владельца закреплены. Можно изменить только название.';return role.permissions.map(p=>permissionLabels[p]).join(' · ')||'Без дополнительных разрешений';}
</script>
<template>
 <section ref="root" class="role-editor">
  <p class="field-help">Участнику можно выдать несколько ролей. Их разрешения складываются.</p>
  <ul class="managed-list role-list">
   <li v-for="role in displayedRoles" :key="role.id" class="role-card" :class="{editing:editing===role.id}">
    <form v-if="editing===role.id" class="inline-role-form" @submit.prevent="save" @keydown.esc.prevent="cancel">
     <div class="role-edit-heading">
      <input v-model="name" class="role-name-input" aria-label="Название роли" placeholder="Название роли" required maxlength="40" :disabled="busy">
      <div v-if="nameOnly(role.id)" class="role-edit-actions"><button class="soft-button" :disabled="busy">Сохранить</button><button type="button" class="text-button" :disabled="busy" @click="cancel">Отмена</button></div>
     </div>
     <small v-if="nameOnly(role.id)">{{describe(role)}}</small>
     <div v-else class="role-expanded">
      <div class="permission-list"><label v-for="(title,id) in permissionLabels" :key="id"><input v-model="permissions" type="checkbox" :value="id" :disabled="busy||!permitted(self,id)"><span>{{title}}</span></label></div>
      <div class="role-edit-actions"><button class="primary" :disabled="busy">Сохранить</button><button type="button" class="text-button" :disabled="busy" @click="cancel">Отмена</button></div>
     </div>
     <p v-if="error" class="form-error" role="alert">{{error}}</p>
    </form>
    <template v-else>
     <div class="role-summary"><strong>{{role.name}}</strong><small>{{describe(role)}}</small></div>
     <div v-if="canEdit(role)" class="member-actions"><button class="soft-button" :disabled="busy" :aria-label="'Изменить роль '+role.name" @click="edit(role)">Изменить</button><button v-if="!['member','admin','owner'].includes(role.id)" class="soft-button" :disabled="busy" :aria-label="'Удалить роль '+role.name" @click="confirmRemoval(role)">Удалить</button></div>
    </template>
    <div v-if="removal?.id===role.id" class="role-removal" role="alert"><h3>Удалить роль «{{role.name}}»?</h3><p class="field-help">Она будет снята со всех участников ({{members.filter(m=>m.roleIds?.includes(role.id)).length}}). Остальные роли сохранятся.</p><p v-if="error" class="form-error">{{error}}</p><div class="role-edit-actions"><button class="danger-button" :disabled="busy" @click="run(()=>mutate('/api/roles/'+role.id,'DELETE'))">Удалить роль</button><button type="button" class="text-button" :disabled="busy" @click="cancel">Отмена</button></div></div>
   </li>
  </ul>
  <button v-if="permitted(self,'manage_roles')&&editing===undefined" class="soft-button create-role" @click="edit()">Создать роль</button>
 </section>
</template>
<style scoped>
.role-list>.role-card{flex-wrap:wrap}.role-summary{min-width:0}.inline-role-form{width:100%;min-width:0;margin:0}.role-edit-heading{display:flex;align-items:center;gap:10px}.role-name-input{flex:1;min-width:0;margin:0;padding:8px 10px;height:34px;font-size:12px;font-weight:500;border-radius:8px}.role-edit-actions{display:flex;align-items:center;gap:8px;flex:none}.role-edit-actions button{font-size:11px;padding:9px 12px;min-height:34px}.inline-role-form>small{margin-top:6px}.role-expanded .permission-list{margin:18px 0}.role-expanded .permission-list label{font-size:12px}.role-removal{flex-basis:100%!important;padding-top:12px;border-top:1px solid #292929}.create-role{margin-top:20px}.role-expanded{animation:role-reveal 140ms ease-out}@keyframes role-reveal{from{opacity:0;transform:translateY(-4px)}to{opacity:1;transform:translateY(0)}}@media(prefers-reduced-motion:reduce){.role-expanded{animation:none}}@media(max-width:500px){.role-edit-heading{flex-wrap:wrap}.role-name-input{flex-basis:100%}.role-edit-actions{flex-wrap:wrap}}
</style>
