import {GtcrnWorkletNode, RnnoiseWorkletNode, loadGtcrn, loadRnnoise} from '@sapphi-red/web-noise-suppressor';
import gtcrnWorklet from '@sapphi-red/web-noise-suppressor/gtcrnWorklet.js?url';
import gtcrnWasm from '@sapphi-red/web-noise-suppressor/gtcrn.wasm?url';
import workletUrl from '@sapphi-red/web-noise-suppressor/rnnoiseWorklet.js?url';
import wasmUrl from '@sapphi-red/web-noise-suppressor/rnnoise.wasm?url';
import simdUrl from '@sapphi-red/web-noise-suppressor/rnnoise_simd.wasm?url';
import type {AudioProcessorOptions,TrackProcessor,Track} from 'livekit-client';
import type {LocalNoiseEngine} from './audioLab';
const engines = {
  rnnoise: {worklet: workletUrl, Node: RnnoiseWorkletNode, load: () => loadRnnoise({url: wasmUrl, simdUrl})},
  gtcrn: {worklet: gtcrnWorklet, Node: GtcrnWorkletNode, load: () => loadGtcrn({url: gtcrnWasm})},
};
const binaries = new Map<LocalNoiseEngine, Promise<ArrayBuffer>>();
const modules = new WeakMap<AudioContext, Map<LocalNoiseEngine, Promise<void>>>();

export async function createNoiseNode(context: AudioContext, engine: LocalNoiseEngine) {
  if (context.sampleRate !== 48000) throw new Error('Expected 48 kHz microphone graph');
  const spec = engines[engine];
  let registered = modules.get(context);
  if (!registered) {
    registered = new Map();
    modules.set(context, registered);
  }
  if (!registered.has(engine)) {
    registered.set(engine, context.audioWorklet.addModule(spec.worklet)
      .catch(error => { registered.delete(engine); throw error; }));
  }
  if (!binaries.has(engine)) {
    binaries.set(engine, spec.load().then(binary => {
      // The worklet constructs the WASM instance asynchronously. Surface an unsupported
      // SIMD build or an invalid asset before publishing a silent microphone track.
      if (!WebAssembly.validate(binary)) throw new Error('Unsupported noise suppression module');
      return binary;
    }).catch(error => { binaries.delete(engine); throw error; }));
  }
  const [wasmBinary] = await Promise.all([binaries.get(engine)!, registered.get(engine)]);
  return new spec.Node(context, {wasmBinary, maxChannels: 1});
}
/** A separate 48 kHz graph handles devices running at any native sample rate. */
export class NimboNoiseProcessor implements TrackProcessor<Track.Kind.Audio,AudioProcessorOptions>{
 constructor(private readonly engine: LocalNoiseEngine) {}
 get name() { return 'nimbo-' + this.engine; }
 processedTrack?:MediaStreamTrack;
 private context?:AudioContext;private source?:MediaStreamAudioSourceNode;private node?:Awaited<ReturnType<typeof createNoiseNode>>;private destination?:MediaStreamAudioDestinationNode;private generation=0;
 async init({track}:AudioProcessorOptions){
  await this.destroy();const generation=this.generation;
  const context=new AudioContext({sampleRate:48000});this.context=context;
  try{
   await context.resume();const node=await createNoiseNode(context, this.engine);
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
