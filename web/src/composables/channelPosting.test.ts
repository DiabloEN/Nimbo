import {describe,it,expect} from 'vitest';
import {canWriteChannel} from './channelPosting';
import type {Channel,Member} from './useWorkspace';
const channel:Channel={id:'news',name:'News',kind:'text',description:'',posting:{mode:'roles',roleIds:['authors']}};
const member:Member={id:'reader',name:'Reader',role:'member'};
describe('channel posting',()=>{
 it('limits writing to selected roles and owner, including legacy admin membership',()=>{
  expect(canWriteChannel(channel,member)).toBe(false);
  expect(canWriteChannel(channel,{...member,roleIds:['authors']})).toBe(true);
  expect(canWriteChannel(channel,{...member,role:'owner'})).toBe(true);
  expect(canWriteChannel(channel,{...member,role:'admin'})).toBe(false);
  expect(canWriteChannel({...channel,posting:{mode:'roles',roleIds:['admin']}},{...member,role:'admin'})).toBe(true);
 });
 it('defaults existing channels to everyone but fails closed for missing membership and empty role selection',()=>{
  expect(canWriteChannel({...channel,posting:undefined},member)).toBe(true);
  expect(canWriteChannel({...channel,posting:{mode:'roles',roleIds:[]}},member)).toBe(false);
  expect(canWriteChannel(channel,undefined)).toBe(false);
  expect(canWriteChannel(channel,{...member,role:'owner',blocked:true})).toBe(false);
 });
});
