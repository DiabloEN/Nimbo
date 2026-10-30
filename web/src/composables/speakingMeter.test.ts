import {afterEach,expect,it,vi} from 'vitest';
import {createSpeakingMeter} from './speakingMeter';
afterEach(()=>{vi.useRealTimers();vi.unstubAllGlobals();});
it('lights within one 50ms sample, holds brief gaps and releases without stopping capture',()=>{
 vi.useFakeTimers();let amplitude=0;const close=vi.fn().mockResolvedValue(undefined),disconnect=vi.fn();
 vi.stubGlobal('AudioContext',class{state='running';close=close;resume=()=>Promise.resolve();createMediaStreamSource(){return{connect:vi.fn(),disconnect}}createAnalyser(){return{disconnect,getFloatTimeDomainData:(a:Float32Array)=>a.fill(amplitude)}}});
 vi.stubGlobal('MediaStream',class{});const capture={readyState:'live',enabled:true,stop:vi.fn()} as unknown as MediaStreamTrack;
 const changes=vi.fn(),meter=createSpeakingMeter(()=>capture,changes);meter.start();vi.advanceTimersByTime(50);amplitude=.05;vi.advanceTimersByTime(50);expect(changes).toHaveBeenLastCalledWith(true);
 amplitude=0;vi.advanceTimersByTime(100);expect(changes).toHaveBeenLastCalledWith(true);vi.advanceTimersByTime(100);expect(changes).toHaveBeenLastCalledWith(false);
 capture.enabled=false;vi.advanceTimersByTime(50);expect(close).toHaveBeenCalledOnce();expect(capture.stop).not.toHaveBeenCalled();meter.stop();expect(vi.getTimerCount()).toBe(0);
});
