import {describe,it,expect} from 'vitest';import {parseProfile} from './profileFile';
const profile={format:'nimbo-profile',version:1,name:'Tester',avatar:'',servers:[{url:'https://dev.example.com',name:'Friends',displayName:'Server alias',profileKey:'a'.repeat(43)}]};
describe('profile file boundary',()=>{
 it('round trips identities without importing role claims, passwords or unrelated local preferences',()=>{const result=parseProfile(JSON.stringify({...profile,role:'owner',password:'secret',volumes:{x:200}}));expect(result).toEqual(profile);});
 it.each(['javascript:alert(1)','https://user:pass@example.com','http://example.com','https://example.com/path','https://example.com/?key=x'])('rejects unsafe server address %s',url=>{expect(()=>parseProfile(JSON.stringify({...profile,servers:[{...profile.servers[0],url}]}))).toThrow();});
 it('rejects scripts as avatars, oversized files and unsupported versions',()=>{for(const patch of [{avatar:'data:image/svg+xml;base64,PHN2Zz4='},{version:2},{servers:[...profile.servers,...profile.servers]}])expect(()=>parseProfile(JSON.stringify({...profile,...patch}))).toThrow();expect(()=>parseProfile(' '.repeat(100001))).toThrow();});
});
