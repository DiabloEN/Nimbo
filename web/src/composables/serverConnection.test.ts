import {afterEach,expect,it,vi} from 'vitest';
import {activeServer,clientOrigin,prepareServer,serverFetch} from './serverConnection';
import {useWorkspace} from './useWorkspace';
const target={url:'https://other.example:7443',name:'Other',displayName:'Me',profileKey:'b'.repeat(43)};
const state={name:'Other',channels:[],members:[],selfId:'me',serverTime:Date.now()};
afterEach(()=>{activeServer.value=clientOrigin;vi.unstubAllGlobals();});
it('prepares a target with only its key, no cookies or redirects; leaves the current server intact',async()=>{
 const fetch=vi.fn().mockResolvedValueOnce({ok:true,json:async()=>({session:'target-token',member:{id:'me',name:'Me'}})}).mockResolvedValueOnce({ok:true,json:async()=>state});vi.stubGlobal('fetch',fetch);
 activeServer.value='https://current.example';
 await prepareServer(target,new AbortController().signal);
 expect(activeServer.value).toBe('https://current.example');
 expect(fetch.mock.calls[0]![0]).toBe(target.url+'/api/profile-session');
 expect(JSON.parse(fetch.mock.calls[0]![1].body)).toEqual({profileKey:target.profileKey});
 for(const [,init]of fetch.mock.calls){expect(init.credentials).toBe('omit');expect(init.redirect).toBe('error');expect(init.headers['X-Nimbo-Client']).toBe('1');}
 expect(fetch.mock.calls[1]![1].headers.Authorization).toBe('Bearer target-token');
});
it('does not change current context when the destination rejects access',async()=>{
 vi.stubGlobal('fetch',vi.fn().mockResolvedValue({ok:false,json:async()=>({error:'Denied'})}));activeServer.value='https://current.example';
 await expect(prepareServer(target,new AbortController().signal)).rejects.toThrow('Denied');expect(activeServer.value).toBe('https://current.example');
});
it('rejects external paths instead of sending a token to another destination',async()=>{
 vi.stubGlobal('fetch',vi.fn());expect(()=>serverFetch(target.url,'https://evil.example/api/workspace',{headers:{Authorization:'Bearer private'}})).toThrow();
 expect(fetch).not.toHaveBeenCalled();
});
it('an old unauthorized request cannot renew or retry with the new server token',async()=>{
 let done!:(value:unknown)=>void;const fetch=vi.fn().mockImplementation(()=>new Promise(resolve=>done=resolve));vi.stubGlobal('fetch',fetch);
 const session={value:'old-token'},renew=vi.fn();const ws=useWorkspace(session,renew);activeServer.value='https://old.example';
 const pending=ws.request('/api/workspace');const result=expect(pending).rejects.toThrow('Запрос отменён');
 ws.stop();activeServer.value=target.url;session.value='new-token';done({ok:false,status:401,json:async()=>({error:'expired'})});await result;
 expect(renew).not.toHaveBeenCalled();expect(fetch).toHaveBeenCalledTimes(1);expect(ws.expired.value).toBe(false);
});
