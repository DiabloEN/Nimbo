import type {Member,Role} from './useWorkspace';
export type Mention={kind:'member'|'role'|'everyone';id:string;label:string;start:number;end:number};
export type MentionChoice=Omit<Mention,'start'|'end'>&{detail:string};
export type MentionNotice={id:string;channel:string;channelName:string;name:string;text:string;createdAt:number};
export function mentionChoices(members:Member[],roles:Role[],selfId:string):MentionChoice[]{
 const owner=members.find(m=>m.id===selfId)?.role==='owner';
 return [{kind:'everyone',id:'everyone',label:'everyone',detail:'Все участники'},
  ...roles.filter(r=>r.id!=='owner'&&(owner||r.id!=='admin'&&!r.permissions.includes('manage_roles'))).map(r=>({kind:'role' as const,id:r.id,label:r.name,detail:'Роль'})),
  ...members.filter(m=>!m.blocked&&m.id!==selfId).map(m=>({kind:'member' as const,id:m.id,label:m.name,detail:m.role==='owner'?(roles.find(r=>r.id==='owner')?.name||'Владелец'):m.role==='admin'?'Администратор':'Участник'}))];
}
// Preserve selected identities through edits; changing any part of a tag removes it.
export function moveMentions(before:string,after:string,mentions:Mention[]):Mention[]{
 let start=0;while(start<before.length&&start<after.length&&before[start]===after[start])start++;
 let end=before.length,nextEnd=after.length;while(end>start&&nextEnd>start&&before[end-1]===after[nextEnd-1]){end--;nextEnd--;}
 const delta=after.length-before.length;
 return mentions.flatMap(m=>m.end<=start?[m]:m.start>=end?[{...m,start:m.start+delta,end:m.end+delta}]:[])
  .filter(m=>after.slice(m.start,m.end)==='@'+m.label);
}
export function withEveryone(text:string,mentions:Mention[]):Mention[]{
 const result=[...mentions];
 for(const match of text.matchAll(/(^|\s)@everyone(?=$|[\s.,!?;:])/g)){
  const start=match.index!+match[1]!.length,end=start+9;
  if(!result.some(m=>m.start<end&&m.end>start))result.push({kind:'everyone',id:'everyone',label:'everyone',start,end});
 }
 return result.sort((a,b)=>a.start-b.start);
}
function addressedTo(mention:Mention,self?:Member){
 if(!self||self.blocked)return false;
 if(mention.kind==='member')return mention.id===self.id;
 if(mention.kind==='everyone')return true;
 const ids=self.roleIds??(self.role==='admin'?['admin']:[]);
 return self.role!=='owner'&&((mention.id==='member'&&self.role==='member')||ids.includes(mention.id));
}
export function messageParts(text:string,mentions:Mention[]=[],self?:Member){
 const parts:{text:string;mention:boolean;personal:boolean}[]=[];let cursor=0;
 for(const m of mentions){if(m.start<cursor||m.end>text.length)continue;parts.push({text:text.slice(cursor,m.start),mention:false,personal:false},{text:text.slice(m.start,m.end),mention:true,personal:addressedTo(m,self)});cursor=m.end;}
 parts.push({text:text.slice(cursor),mention:false,personal:false});return parts;
}
