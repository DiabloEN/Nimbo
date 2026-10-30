<script setup lang="ts">
import {computed,onBeforeUnmount,onMounted,ref} from 'vue';
import {spectrumBars} from '../composables/audioSpectrum';
import AppIcon from './AppIcon.vue';
const props=defineProps<{src?:string;name:string;downloading?:boolean}>();
const emit=defineEmits<{download:[];error:[]}>();
const audio=ref<HTMLAudioElement>(),playing=ref(false),position=ref(0),duration=ref(0),waiting=ref(false);
const progress=computed(()=>duration.value?Math.min(100,position.value/duration.value*100):0);
// Real frequency levels from this media element only; no microphone or full-file decoding.
const bars=ref(Array.from({length:64},(_,i)=>16+((i*17)%28)));
let context:AudioContext|undefined,analyser:AnalyserNode|undefined,source:MediaElementAudioSourceNode|undefined,frame=0,lastFrame=0,disposed=false;
const levels=new Float32Array(1024);
const reduced=window.matchMedia('(prefers-reduced-motion: reduce)');
async function prepareAudio(){
 if(!audio.value)return;
 if(!context&&typeof AudioContext!=='undefined'){
  context=new AudioContext();analyser=context.createAnalyser();analyser.fftSize=2048;analyser.smoothingTimeConstant=.65;
  source=context.createMediaElementSource(audio.value);source.connect(analyser);analyser.connect(context.destination);
 }
 if(context?.state==='suspended')await context.resume();
}
function draw(time:number){
 if(disposed||!playing.value)return;
 if(analyser&&!document.hidden&&!reduced.matches&&time-lastFrame>45){
  analyser.getFloatFrequencyData(levels);
  bars.value=spectrumBars(levels,context!.sampleRate,analyser.fftSize,bars.value);lastFrame=time;
 }
 frame=requestAnimationFrame(draw);
}
function paused(){playing.value=false;cancelAnimationFrame(frame);void context?.suspend().catch(()=>{});}
function clock(value:number){if(!Number.isFinite(value)||value<0)return '0:00';const s=Math.floor(value);return `${Math.floor(s/60)}:${String(s%60).padStart(2,'0')}`;}
function metadata(){const value=audio.value?.duration;duration.value=value&&Number.isFinite(value)?value:0;}
async function toggle(){const el=audio.value;if(!el||!props.src)return;if(!el.paused){el.pause();return;}try{await prepareAudio();if(disposed)return;await el.play();}catch(e){if((e as Error).name!=='AbortError')emit('error');}}
function seek(e:Event){const el=audio.value;if(!el||!duration.value)return;position.value=Number((e.target as HTMLInputElement).value);el.currentTime=position.value;}
function started(){playing.value=true;cancelAnimationFrame(frame);frame=requestAnimationFrame(draw);window.dispatchEvent(new CustomEvent('nimbo-audio-play',{detail:audio.value}));}
function otherStarted(e:Event){if((e as CustomEvent).detail!==audio.value)audio.value?.pause();}
onMounted(()=>window.addEventListener('nimbo-audio-play',otherStarted));
onBeforeUnmount(()=>{window.removeEventListener('nimbo-audio-play',otherStarted);disposed=true;audio.value?.pause();cancelAnimationFrame(frame);source?.disconnect();analyser?.disconnect();void context?.close().catch(()=>{});});
</script>
<template>
 <div class="audio-card" :class="{playing}" :aria-label="name">
  <audio ref="audio" :src="src||undefined" preload="metadata" @loadedmetadata="metadata" @durationchange="metadata" @timeupdate="position=audio?.currentTime||0" @play="started" @pause="paused" @ended="paused" @waiting="waiting=true" @playing="waiting=false" @canplay="waiting=false" @error="emit('error')"/>
  <button type="button" class="audio-play" :disabled="!src" :aria-label="playing?'РџСЂРёРѕСЃС‚Р°РЅРѕРІРёС‚СЊ '+name:'Р’РѕСЃРїСЂРѕРёР·РІРµСЃС‚Рё '+name" @click="toggle"><svg width="22" height="22" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path v-if="playing" d="M6 4h4v16H6zm8 0h4v16h-4z"/><path v-else d="M8 4v16l12-8z"/></svg></button>
  <div class="audio-main">
   <div class="audio-title"><span :title="name">{{name}}</span><button type="button" class="audio-download" :disabled="downloading" :aria-label="'РЎРєР°С‡Р°С‚СЊ '+name" :title="downloading?'Р—Р°РіСЂСѓР·РєР°вЂ¦':'РЎРєР°С‡Р°С‚СЊ'" @click="emit('download')"><AppIcon name="download" :size="16"/></button></div>
   <div class="audio-timeline">
    <div class="equalizer" aria-hidden="true"><span v-for="(height,i) in bars" :key="i" class="bar" :style="{height:height+'%'}"/></div>
    <div class="equalizer-progress" :style="{width:progress+'%'}" aria-hidden="true"><div class="equalizer"><span v-for="(height,i) in bars" :key="i" class="bar" :style="{height:height+'%'}"/></div></div>
    <input type="range" min="0" :max="duration||1" step="0.1" :value="position" :disabled="!duration" :aria-label="'РџРѕР·РёС†РёСЏ РІРѕСЃРїСЂРѕРёР·РІРµРґРµРЅРёСЏ: '+name" :aria-valuetext="clock(position)+' РёР· '+clock(duration)" @input="seek">
   </div>
   <div class="audio-time"><span>{{clock(position)}}<span v-if="waiting" role="status" aria-label="Р‘СѓС„РµСЂРёР·Р°С†РёСЏ"> В·В·В·</span></span><span>{{clock(duration)}}</span></div>
  </div>
 </div>
</template>
<style scoped>
.audio-card{display:flex;align-items:center;gap:14px;width:480px;max-width:100%;box-sizing:border-box;padding:14px 16px;background:#1b1d1d;border-radius:20px;color:#e5e8e7}.audio-card audio{display:none}.audio-play{display:flex;align-items:center;justify-content:center;flex:none;width:42px;height:42px;border:0;border-radius:50%;background:#dce5e3;color:#1b2421;padding:0;cursor:pointer}.audio-play:hover{background:#f2f8f6}.audio-play:disabled{opacity:.5;cursor:default}.audio-main{flex:1;min-width:0}.audio-title{display:flex;align-items:center;gap:8px;margin-bottom:6px;font-size:12px}.audio-title>span{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.audio-download{display:flex;align-items:center;justify-content:center;flex:none;border:0;background:none;color:#8d9994;padding:4px;cursor:pointer;border-radius:5px}.audio-download:hover{color:#e1f5eb;background:#ffffff0b}.audio-timeline{position:relative;height:34px;container-type:inline-size;border-radius:4px}.equalizer{display:flex;align-items:flex-end;justify-content:space-between;gap:clamp(1px,.7cqw,3px);width:100%;height:100%}.bar{flex:1 1 0;max-width:2px;min-width:1px;border-radius:999px;transition:height 65ms linear;background:#62656b}.equalizer-progress{position:absolute;inset:0 auto 0 0;overflow:hidden;pointer-events:none}.equalizer-progress .equalizer{width:100cqw}.equalizer-progress .bar{background:#d2d8e2}.audio-timeline input{position:absolute;inset:0;width:100%;height:100%;opacity:0;cursor:pointer;margin:0;padding:0;border:0;appearance:none;background:transparent}.audio-timeline input::-webkit-slider-runnable-track{width:100%;height:100%;border:0;padding:0}.audio-timeline input::-webkit-slider-thumb{appearance:none;width:0;height:100%;border:0;padding:0}.audio-timeline input::-moz-range-track{width:100%;height:100%;border:0;padding:0}.audio-timeline input::-moz-range-thumb{width:0;height:100%;border:0;padding:0}.audio-timeline:has(input:focus-visible){outline:2px solid #d2d8e2;outline-offset:4px}.audio-time{display:flex;justify-content:space-between;font-size:10px;color:#999da5;margin-top:5px;font-variant-numeric:tabular-nums}.audio-play:focus-visible,.audio-download:focus-visible{outline:2px solid #d2d8e2;outline-offset:3px}@media(prefers-reduced-motion:reduce){.bar{transition:none}}@media(max-width:450px){.audio-card{padding:12px;gap:10px}.audio-play{width:36px;height:36px}.bar{max-width:2px}}
</style>
