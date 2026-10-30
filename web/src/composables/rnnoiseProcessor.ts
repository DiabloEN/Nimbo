import {RnnoiseWorkletNode,loadRnnoise} from '@sapphi-red/web-noise-suppressor';
import workletUrl from '@sapphi-red/web-noise-suppressor/rnnoiseWorklet.js?url';
import wasmUrl from '@sapphi-red/web-noise-suppressor/rnnoise.wasm?url';
import simdUrl from '@sapphi-red/web-noise-suppressor/rnnoise_simd.wasm?url';
import type {AudioProcessorOptions,TrackProcessor,Track} from 'livekit-client';
let binary:Promise<ArrayBuffer>|undefined;
const modules=new WeakMap<AudioContext,Promise<void>>();
export async function createNoiseNode(context:AudioContext){
 if(context.sampleRate!==48000)throw new Error('RNNoise requires 48 kHz');
 binary??=loadRnnoise({url:wasmUrl,simdUrl}).catch(e=>{binary=undefined;throw e;});
 if(!modules.has(context))modules.set(context,context.audioWorklet.addModule(workletUrl).catch(e=>{modules.delete(context);throw e;}));
 const [wasmBinary]=await Promise.all([binary,modules.get(context)]);
 return new RnnoiseWorkletNode(context,{wasmBinary,maxChannels:1});
}
/** A separate 48 kHz graph handles devices running at any native sample rate. */
export class NimboNoiseProcessor implements TrackProcessor<Track.Kind.Audio,AudioProcessorOptions>{
 name='nimbo-rnnoise';processedTrack?:MediaStreamTrack;
 private context?:AudioContext;private source?:MediaStreamAudioSourceNode;private node?:RnnoiseWorkletNode;private destination?:MediaStreamAudioDestinationNode;private generation=0;
 async init({track}:AudioProcessorOptions){
  await this.destroy();const generation=this.generation;
  const context=new AudioContext({sampleRate:48000});this.context=context;
  try{
   await context.resume();const node=await createNoiseNode(context);
   if(generation!==this.generation){node.destroy();node.disconnect();return;}
   this.node=node;this.source=context.createMediaStreamSource(new MediaStream([track]));
   this.destination=context.createMediaStreamDestination();
   this.source.connect(node).connect(this.destination);this.processedTrack=this.destination.stream.getAudioTracks()[0];
   this.processedTrack!.enabled=track.enabled;
  }catch(e){await this.destroy();throw e;}
 }
 async restart(options:AudioProcessorOptions){await this.init(options);}
 async destroy(){
  ++this.generation;this.source?.disconnect();this.node?.disconnect();this.node?.destroy();this.destination?.disconnect();this.processedTrack?.stop();
  const context=this.context;this.context=undefined;this.source=undefined;this.node=undefined;this.destination=undefined;this.processedTrack=undefined;
  if(context&&context.state!=='closed')await context.close();
 }
}
