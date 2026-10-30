import { afterEach, expect, it, vi } from 'vitest';
import { useAccess } from './useAccess';
afterEach(() => vi.unstubAllGlobals());
it('rejects an invalid shared code, then accepts a verified name without opening media', async () => {
  const fetchMock = vi.fn().mockResolvedValueOnce({ ok:false, json:async()=>({error:'Неверный код доступа'}) })
    .mockResolvedValueOnce({ ok:true, json:async()=>({name:'Tester'}) });
  vi.stubGlobal('fetch', fetchMock);
  const access = useAccess();
  expect(await access.signIn('Tester', 'wrong')).toBe(false);
  expect(access.authenticated.value).toBe(false);
  expect(access.error.value).toBe('Неверный код доступа');
  expect(await access.signIn(' Tester ', 'valid-test-code')).toBe(true);
  expect(fetchMock.mock.calls[1]![0]).toBe('/api/auth');
  expect(JSON.parse(fetchMock.mock.calls[1]![1].body)).toEqual({name:'Tester',password:'valid-test-code'});
  expect(access.authenticated.value).toBe(true);
  access.reset(); expect(access.authenticated.value).toBe(false);
});
it('cannot restore access from a late response after sign-out', async () => {
  let resolve!: (value: unknown) => void;
  vi.stubGlobal('fetch', vi.fn(() => new Promise(done => {resolve=done;})));
  const access = useAccess(); const pending=access.signIn('Tester','test-code');
  access.reset(); resolve({ok:true,json:async()=>({name:'Tester'})});
  expect(await pending).toBe(false);
  expect(access.authenticated.value).toBe(false);
  expect(access.authenticating.value).toBe(false);
});
it('restores a server-verified cookie session without a password and revokes it on logout',async()=>{
 const fetchMock=vi.fn().mockResolvedValueOnce({ok:true,json:async()=>({session:'bearer',member:{name:'Admin'}})}).mockResolvedValueOnce({ok:true});vi.stubGlobal('fetch',fetchMock);
 const access=useAccess();expect(await access.restore()).toBe(true);expect(access.member.value?.name).toBe('Admin');expect(access.session.value).toBe('bearer');expect(fetchMock.mock.calls[0]![0]).toBe('/api/session');
 expect(await access.signOut()).toBe(true);expect(access.authenticated.value).toBe(false);expect(fetchMock.mock.calls[1]![1].method).toBe('DELETE');
});
it('does not trust an expired session or a restore response arriving after reset',async()=>{
 vi.stubGlobal('fetch',vi.fn().mockResolvedValue({status:401,ok:false}));const expired=useAccess();expect(await expired.restore()).toBe(false);expect(expired.authenticated.value).toBe(false);
 let resolve!:(value:unknown)=>void;vi.stubGlobal('fetch',vi.fn(()=>new Promise(done=>{resolve=done;})));const access=useAccess(),pending=access.restore();access.reset();resolve({ok:true,json:async()=>({session:'late',member:{name:'Late'}})});expect(await pending).toBe(false);expect(access.authenticated.value).toBe(false);
});
