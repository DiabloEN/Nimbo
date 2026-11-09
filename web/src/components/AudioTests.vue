<script setup lang="ts">
import {ref,watch,onBeforeUnmount} from 'vue';
import type {LocalNoiseEngine} from '../composables/audioLab';
const props=defineProps<{noiseEngine?:LocalNoiseEngine;captureOptions?:{echoCancellation:boolean;noiseSuppression:boolean;autoGainControl:boolean};inputId:string;outputId:string;active:boolean;inCall:boolean}>();
let noiseNode:{disconnect:()=>void;destroy:()=>void}|undefined;
const mode=ref(''),note=ref(''),level=ref(0);let context:AudioContext|undefined,stream:MediaStream|undefined,player:HTMLAudioElement|undefined,timer:ReturnType<typeof setTimeout>|undefined,frame=0,generation=0;
function stop(){generation++;noiseNode?.disconnect();noiseNode?.destroy();noiseNode=undefined;clearTimeout(timer);cancelAnimationFrame(frame);stream?.getTracks().forEach(t=>t.stop());stream=undefined;player?.pause();if(player)player.srcObject=null;player=undefined;void context?.close();context=undefined;mode.value='';level.value=0;}
async function start(kind:'mic'|'sound'){
 if(!props.active||(kind==='mic'&&props.inCall))return;
 if(mode.value){stop();return;}stop();const run=generation;mode.value=kind;note.value='';
 try{
  const ctx=new AudioContext({sampleRate:48000});context=ctx;void ctx.resume();const destination=ctx.createMediaStreamDestination();
  const audio=new Audio();player=audio;audio.srcObject=destination.stream;
  if('setSinkId' in audio)await (audio as HTMLAudioElement&{setSinkId:(id:string)=>Promise<void>}).setSinkId(props.outputId);
  if(run!==generation){destination.stream.getTracks().forEach(t=>t.stop());return;}
  if(kind==='mic'){
   const capture=await navigator.mediaDevices.getUserMedia({audio:{deviceId:props.inputId==='default'?'default':{exact:props.inputId},...(props.captureOptions||{echoCancellation:true,noiseSuppression:true,autoGainControl:false})}});
   if(run!==generation){capture.getTracks().forEach(t=>t.stop());return;}stream=capture;
   const source=ctx.createMediaStreamSource(capture),analyser=ctx.createAnalyser();analyser.fftSize=256;let processed:AudioNode=source;
   if(import.meta.env.VITE_AUDIO_LAB!=='0'&&props.noiseEngine){const {createNoiseNode}=await import('../composables/rnnoiseProcessor');const node=await createNoiseNode(ctx, props.noiseEngine);if(run!==generation){node.destroy();node.disconnect();return;}noiseNode=node;source.connect(node);processed=node;}
   processed.connect(analyser);processed.connect(destination);
   const data=new Uint8Array(analyser.fftSize);const draw=()=>{if(run!==generation)return;analyser.getByteTimeDomainData(data);level.value=Math.min(100,Math.sqrt(data.reduce((sum,x)=>sum+(x-128)**2,0)/data.length)*3);frame=requestAnimationFrame(draw);};draw();
   note.value='Вы слышите себя. Проверка остановится через 10 секунд.';
  }else{
   // A short major phrase with a soft harmonic layer; peaks stay below clipping.
   const phrase=[392,493.88,587.33,659.25,587.33,493.88,392];
   for(const [index,hz] of phrase.entries())for(const harmonic of [1,2]){const oscillator=ctx.createOscillator(),gain=ctx.createGain();const at=ctx.currentTime+0.06+index*0.34,duration=index===phrase.length-1?0.8:0.42;oscillator.frequency.value=hz*harmonic;oscillator.type='sine';gain.gain.setValueAtTime(0,at);gain.gain.linearRampToValueAtTime(harmonic===1?0.23:0.045,at+0.025);gain.gain.exponentialRampToValueAtTime(0.001,at+duration);oscillator.connect(gain).connect(destination);oscillator.start(at);oscillator.stop(at+duration+0.02);}
  }
  await audio.play();if(run!==generation)return;timer=setTimeout(stop,kind==='mic'?10000:3000);
 }catch{if(run===generation){stop();note.value='Не удалось запустить проверку. Проверьте разрешения и выбранные устройства.';}}
}
watch(()=>[props.active,props.inputId,props.outputId,props.inCall,props.noiseEngine,props.captureOptions?.noiseSuppression,props.captureOptions?.autoGainControl],stop);onBeforeUnmount(stop);
</script>
<template><div class="audio-tests"><div class="button-row"><button type="button" class="soft-button" :disabled="inCall||mode==='sound'" @click="start('mic')">{{mode==='mic'?'Остановить проверку':'Проверить микрофон'}}</button><button type="button" class="soft-button" :disabled="mode==='mic'" @click="start('sound')">{{mode==='sound'?'Остановить звук':'Проверить звук'}}</button></div><progress v-if="mode==='mic'" :value="level" max="100" aria-label="Уровень микрофона"></progress><p v-if="inCall" class="field-help">Для прослушивания своего микрофона выйдите из голосовой комнаты.</p><p v-else class="field-help">Микрофон лучше проверять в наушниках.</p><p v-if="note" class="field-help" role="status">{{note}}</p></div></template>
