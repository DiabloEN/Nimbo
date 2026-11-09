import { afterEach, expect, it, vi } from 'vitest';
import { useWorkspace } from './useWorkspace';
afterEach(()=>vi.unstubAllGlobals());
function writer(){const ws=useWorkspace({value:'session'});ws.selfId.value='me';ws.members.value=[{id:'me',name:'Me',role:'member'}];ws.channels.value=[{id:'general',name:'General',kind:'text',description:''}];return ws;}
const ok=(data:unknown)=>({ok:true,json:async()=>data});
const state=(name:string)=>({name,channels:[],members:[],selfId:'me',ownerExists:true,presence:{},presenceAvailable:true,serverTime:Date.now()});
it('sends file-only messages as multipart and retries with the same client id',async()=>{
 const uploads:FormData[]=[];let fail=true;
 vi.stubGlobal('fetch',vi.fn(async(path:string,options?:RequestInit)=>{
  if(path.endsWith('/files')){
   expect((options?.headers as Record<string,string>)['Content-Type']).toBeUndefined();
   const body=options?.body as FormData;uploads.push(body);
   if(fail){fail=false;throw new Error('lost connection');}
   return ok({id:'accepted',authorId:'me',name:'Me',text:'',createdAt:1,clientId:body.get('clientId'),attachment:{id:'blob',name:'notes.txt',size:5,scanned:false}});
  }
  return ok({messages:[],hasMore:false});
 }));
 const ws=writer();ws.textChannel.value='general';
 const file=new File(['hello'],'notes.txt'),second=new File(['two'],'second.txt');
 expect(await ws.send('','',[],[file,second])).toBe(false);
 expect(await ws.send('','',[],[file,second])).toBe(true);
 expect(uploads[0]!.get('clientId')).toBe(uploads[1]!.get('clientId'));
 expect(uploads[1]!.getAll('file').map(f=>(f as File).name)).toEqual(['notes.txt','second.txt']);
 ws.stop();
});
it('hides activity for an empty room, including a stale since timestamp',()=>{
 const ws=useWorkspace({value:'session'});ws.presenceAvailable.value=true;
 ws.presence.value={lounge:{since:Date.now()-60000,people:[]}};
 expect(ws.duration('lounge')).toBe('');ws.stop();
});
it('reorders immediately, ignores stale polls and restores order on save failure',async()=>{
 let rejectSave!:(e:Error)=>void;
 const channels=[{id:'a',name:'A',kind:'voice' as const,description:''},{id:'b',name:'B',kind:'voice' as const,description:''}];
 const snapshot={...state('Test'),channels,sectionOrder:['voice','text']};
 vi.stubGlobal('fetch',vi.fn().mockImplementationOnce(()=>new Promise((_resolve,reject)=>rejectSave=reject)).mockResolvedValue(ok(snapshot)));
 const ws=useWorkspace({value:'session'});ws.channels.value=channels;
 const mutation=ws.mutate('/api/channel-order','PATCH',{channels:['b','a'],sections:['text','voice']});
 const outcome=expect(mutation).rejects.toThrow('Сервер не отвечает');
 expect(ws.channels.value.map(c=>c.id)).toEqual(['b','a']);
 await ws.refresh();expect(ws.channels.value.map(c=>c.id)).toEqual(['b','a']);
 rejectSave(new Error('Save failed'));await outcome;
 expect(ws.channels.value.map(c=>c.id)).toEqual(['a','b']);expect(ws.sectionOrder.value).toEqual(['voice','text']);ws.stop();
});
it('does not overwrite a newer workspace response with an old poll',async()=>{
  let done!:(value:unknown)=>void;
  vi.stubGlobal('fetch',vi.fn().mockImplementationOnce(()=>new Promise(resolve=>done=resolve)).mockResolvedValueOnce(ok(state('New'))));
  const ws=useWorkspace({value:'session'});const first=ws.refresh();await ws.refresh();done(ok(state('Old')));await first;
  expect(ws.name.value).toBe('New');ws.stop();
});
it('cannot display late messages from the previously selected channel',async()=>{
  let done!:(value:unknown)=>void;
  vi.stubGlobal('fetch',vi.fn().mockImplementationOnce(()=>new Promise(resolve=>done=resolve)).mockResolvedValueOnce(ok([{id:'new'}])));
  const ws=useWorkspace({value:'session'});const first=ws.selectText('one');await ws.selectText('two');done(ok([{id:'old'}]));await first;
  expect(ws.messages.value.map(m=>m.id)).toEqual(['new']);ws.stop();
});
it('retains an idempotency key after a lost send response',async()=>{
  const fetcher=vi.fn().mockResolvedValueOnce(ok([])).mockRejectedValueOnce(new Error('Lost response'))
    .mockResolvedValueOnce(ok({id:'accepted'})).mockResolvedValueOnce(ok([{id:'accepted'}]));
  vi.stubGlobal('fetch',fetcher);const ws=writer();await ws.selectText('general');
  expect(await ws.send('Hello')).toBe(false);expect(await ws.send('Hello')).toBe(true);
  const first=JSON.parse(fetcher.mock.calls[1]![1].body),retry=JSON.parse(fetcher.mock.calls[2]![1].body);
  expect(retry.clientId).toBe(first.clientId);expect(first.clientId).toHaveLength(43);ws.stop();
});
it('does not report an accepted message as failed when history refresh fails',async()=>{
  vi.stubGlobal('fetch',vi.fn().mockResolvedValueOnce(ok([])).mockResolvedValueOnce(ok({id:'accepted',text:'Hello'})).mockRejectedValueOnce(new Error('History unavailable')));
  const ws=writer();await ws.selectText('general');expect(await ws.send('Hello')).toBe(true);
  expect(ws.messages.value.some(m=>m.id==='accepted')).toBe(true);ws.stop();
});
it('does not restore workspace data after sign-out',async()=>{
  let done!:(value:unknown)=>void;vi.stubGlobal('fetch',vi.fn(()=>new Promise(resolve=>done=resolve)));
  const ws=useWorkspace({value:'session'});const request=ws.refresh();ws.stop();done(ok(state('Late')));await request;
  expect(ws.ready.value).toBe(false);expect(ws.members.value).toEqual([]);
});

it('does not send a draft after posting permission is revoked',async()=>{const fetcher=vi.fn();vi.stubGlobal('fetch',fetcher);const ws=writer();ws.textChannel.value='general';ws.channels.value[0]!.posting={mode:'roles',roleIds:['admin']};expect(await ws.send('Draft')).toBe(false);expect(fetcher).not.toHaveBeenCalled();ws.stop();});

it('shows failed uploads in the outbox and retries their original identity after another send',async()=>{
 const attempts:FormData[]=[];let fail=true;
 vi.stubGlobal('fetch',vi.fn(async(path:string,options?:RequestInit)=>{
  if(path.endsWith('/files')){const b=options!.body as FormData;attempts.push(b);if(fail){fail=false;throw Error('offline');}return ok({id:'file-message',clientId:b.get('clientId')});}
  if(options?.method==='POST')return ok({id:'text-message'});return ok({messages:[]});
 }));
 const ws=writer();ws.textChannel.value='general';const upload=ws.send('caption','',[],[new File(['hi'],'a.txt')]);
 expect(ws.pendingMessages.value[0]?.status).toBe('uploading');await upload;
 const pending=ws.pendingMessages.value[0]!;expect(pending.status).toBe('failed');expect(pending.error).toBeTruthy();
 await ws.send('another');await ws.retryPending(pending);
 expect(attempts[0]!.get('clientId')).toBe(attempts[1]!.get('clientId'));expect(ws.pendingMessages.value).toHaveLength(0);ws.stop();
});

it('loads every continuation when opening an old mention',async()=>{
 const ids=Array.from({length:350},(_,i)=>({id:String(i)}));
 const mock=vi.fn(async(path:string)=>{
   const from=new URL(path,'https://client.example').searchParams.get('from');
   const start=from===null?250:Number(from),end=Math.min(start+100,ids.length);
   return ok({messages:ids.slice(start,end),nextFrom:end<ids.length?String(end):'',hasMore:start>0});
 });
 vi.stubGlobal('fetch',mock);const ws=writer();
 await ws.revealMessage('general','0');
 expect(ws.messages.value.map(m=>m.id)).toEqual(ids.map(m=>m.id));
 expect(ws.hasMore.value).toBe(false);
 ws.stop();
});
