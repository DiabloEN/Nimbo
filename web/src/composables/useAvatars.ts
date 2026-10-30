import {ref,watch,onBeforeUnmount,type Ref} from 'vue';
import type {Member} from './useWorkspace';
export function useAvatars(session:Ref<string>,members:Ref<Member[]>){
 const urls=ref<Record<string,string>>({}),versions=new Map<string,string>();let controller=new AbortController();
 function clear(){controller.abort();controller=new AbortController();Object.values(urls.value).forEach(URL.revokeObjectURL);urls.value={};versions.clear();}
 watch(session,clear);
 watch(members,()=>{const token=session.value;if(!token)return;for(const m of members.value){if(versions.get(m.id)===(m.avatarVersion||''))continue;versions.set(m.id,m.avatarVersion||'');if(urls.value[m.id])URL.revokeObjectURL(urls.value[m.id]!);delete urls.value[m.id];if(!m.avatarVersion)continue;
 const version=m.avatarVersion;void fetch(`/api/avatars/${encodeURIComponent(m.id)}`,{headers:{Authorization:`Bearer ${token}`},signal:controller.signal}).then(async r=>{if(!r.ok)throw new Error();const blob=await r.blob();if(token===session.value&&versions.get(m.id)===version)urls.value[m.id]=URL.createObjectURL(blob);}).catch(()=>{if(token===session.value&&versions.get(m.id)===version)versions.delete(m.id);});}});
 onBeforeUnmount(clear);return urls;
}
