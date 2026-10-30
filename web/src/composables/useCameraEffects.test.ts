import {beforeEach,describe,it,expect,vi} from 'vitest';import {useCameraEffects} from './useCameraEffects';
const mock=vi.hoisted(()=>({supported:true,failInit:false}));
vi.mock('@livekit/track-processors',()=>({supportsBackgroundProcessors:()=>mock.supported,BackgroundProcessor:(options:any)=>({name:'test-blur',mode:options.mode,processedTrack:{id:'stable-output'},destroy:vi.fn(async()=>{}),switchTo:vi.fn(async function(this:any,next:any){this.mode=next.mode;})})}));
function camera(){let processor:any;return {getProcessor:()=>processor,setProcessor:vi.fn(async(p:any)=>{if(mock.failInit)throw new Error('init failed');processor=p;}),stopProcessor:vi.fn(async()=>{processor=undefined;}),stop:vi.fn()};}
describe('camera effects lifecycle',()=>{
 beforeEach(()=>{mock.supported=true;mock.failInit=false;});
 it('prepares passthrough at startup and retains the exact output through repeated effect changes',async()=>{
  const track=camera();const effects=useCameraEffects(()=>track as any);await effects.apply();
  const processor=track.getProcessor(),output=processor.processedTrack;
  expect(processor.mode).toBe('disabled');
  for(const mode of ['background-blur','disabled','background-blur','disabled']){
   await effects.toggle();expect(processor.mode).toBe(mode);expect(track.getProcessor().processedTrack).toBe(output);
  }
  expect(track.setProcessor).toHaveBeenCalledOnce();expect(processor.switchTo).toHaveBeenCalledTimes(4);
  expect(track.stopProcessor).not.toHaveBeenCalled();expect(track.stop).not.toHaveBeenCalled();expect(processor.destroy).not.toHaveBeenCalled();
 });
 it('removes a processor whose initialization completes after leaving',async()=>{
  let finish!:()=>void;let current:any;const track=camera();
  const install=track.setProcessor.getMockImplementation()!;
  track.setProcessor.mockImplementation(async p=>{await new Promise<void>(r=>finish=r);await install(p);});
  current=track;const effects=useCameraEffects(()=>current);const task=effects.toggle();
  await vi.waitFor(()=>expect(track.setProcessor).toHaveBeenCalled());current=undefined;finish();await task;
  expect(track.stopProcessor).toHaveBeenCalledOnce();expect(effects.busy.value).toBe(false);
 });
 it('preserves a working stream and selection when changing mode fails',async()=>{
  const track=camera(),effects=useCameraEffects(()=>track as any);await effects.toggle();
  const processor=track.getProcessor();processor.switchTo.mockRejectedValueOnce(new Error('switch failed'));
  await effects.toggle();expect(effects.enabled.value).toBe(true);expect(effects.note.value).not.toBe('');
  expect(track.getProcessor()).toBe(processor);expect(track.stopProcessor).not.toHaveBeenCalled();
 });
 it('leaves unsupported cameras usable without requiring effects',async()=>{
  mock.supported=false;const track=camera(),effects=useCameraEffects(()=>track as any);await effects.apply();
  expect(effects.note.value).toBe('');await effects.toggle();expect(effects.enabled.value).toBe(false);
  expect(track.setProcessor).not.toHaveBeenCalled();expect(track.stop).not.toHaveBeenCalled();
 });
 it('releases a failed installation and allows retry with the camera still running',async()=>{
  mock.failInit=true;const track=camera(),effects=useCameraEffects(()=>track as any);await effects.toggle();
  expect(track.setProcessor.mock.calls[0]![0].destroy).toHaveBeenCalledOnce();expect(effects.enabled.value).toBe(false);
  expect(track.stop).not.toHaveBeenCalled();mock.failInit=false;await effects.toggle();expect(effects.enabled.value).toBe(true);
 });
});
