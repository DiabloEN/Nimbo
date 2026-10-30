import {ref} from 'vue';
import {blankProfile,parseProfile,readProfile,storeProfile,type ProfileFile} from './profileFile';
export type LocalProfile={id:string;profile:ProfileFile};
const storageKey='nimbo-profile-library';
export function useProfileLibrary(){
 const entries=ref<LocalProfile[]>([]),activeId=ref('');
 try{
  const saved=JSON.parse(localStorage.getItem(storageKey)||'null');
  if(saved&&Array.isArray(saved.entries)&&saved.entries.length<=16){
   for(const entry of saved.entries){try{if(typeof entry.id==='string'&&!entries.value.some(p=>p.id===entry.id))entries.value.push({id:entry.id,profile:parseProfile(JSON.stringify(entry.profile))});}catch{/* Preserve other readable profiles. */}}
   if(entries.value.some(p=>p.id===saved.activeId))activeId.value=saved.activeId;
  }
 }catch{/* Migrate the existing single profile below. */}
 if(!activeId.value){const old=readProfile();if(old.name){const entry={id:crypto.randomUUID(),profile:old};entries.value.push(entry);activeId.value=entry.id;}}
 function persist(next:LocalProfile[],id:string){
  try{localStorage.setItem(storageKey,JSON.stringify({entries:next,activeId:id}));}catch{throw new Error('Браузер не сохранил изменения. Освободите место или скачайте профиль.');}
  entries.value=next;activeId.value=id;
  // Compatibility mirror; the library is authoritative for this client.
  try{storeProfile(next.find(p=>p.id===id)?.profile||blankProfile());}catch{}
 }
 function save(profile:ProfileFile){if(!activeId.value&&entries.value.length>=16)throw new Error('Можно хранить до 16 профилей. Сначала удалите ненужный.');const id=activeId.value||crypto.randomUUID();persist(entries.value.some(p=>p.id===id)?entries.value.map(p=>p.id===id?{id,profile}:p):[...entries.value,{id,profile}],id);}
 function add(profile:ProfileFile){
  const clean=parseProfile(JSON.stringify(profile));
  const existing=entries.value.find(p=>JSON.stringify(p.profile)===JSON.stringify(clean)||clean.servers.some(s=>p.profile.servers.some(old=>old.url===s.url&&old.profileKey===s.profileKey)));
  // An imported backup never silently overwrites a saved profile.
  if(existing)return existing.id;
  if(entries.value.length>=16)throw new Error('Можно хранить до 16 профилей. Удалите ненужный перед загрузкой нового.');
  const id=crypto.randomUUID();persist([...entries.value,{id,profile:clean}],activeId.value);return id;
 }
 function select(id:string){if(!entries.value.some(p=>p.id===id))throw new Error('Профиль не найден.');persist(entries.value,id);}
 function remove(id:string){persist(entries.value.filter(p=>p.id!==id),activeId.value===id?'':activeId.value);}
 return {entries,activeId,save,add,select,remove,current:()=>entries.value.find(p=>p.id===activeId.value)?.profile};
}
