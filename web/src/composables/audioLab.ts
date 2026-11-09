import {ref} from 'vue';
export const audioLabEnabled=import.meta.env.VITE_AUDIO_LAB==='1';
const localNoiseEngines = ['rnnoise', 'speex', 'gtcrn'] as const;
export type LocalNoiseEngine = typeof localNoiseEngines[number];
const defaultEngine: LocalNoiseEngine = 'gtcrn';
export function isLocalNoiseEngine(value: unknown): value is LocalNoiseEngine {
  return localNoiseEngines.some(engine => engine === value);
}
export function useAudioLab(){
  const suppression=ref(true),gain=ref(true),engine=ref<'browser'|LocalNoiseEngine>(defaultEngine);
  if(audioLabEnabled)try{const p=JSON.parse(localStorage.getItem('nimbo-audio-lab')||'{}');suppression.value=p.suppression!==false;gain.value=p.gain!==false;if(p.engine==='browser'||isLocalNoiseEngine(p.engine))engine.value=p.engine;}catch{}
  const captureOptions=()=>({echoCancellation:true,noiseSuppression:audioLabEnabled?suppression.value&&engine.value==='browser':true,autoGainControl:audioLabEnabled?gain.value:true});
  function save(){if(audioLabEnabled)try{localStorage.setItem('nimbo-audio-lab',JSON.stringify({suppression:suppression.value,gain:gain.value,engine:engine.value}));}catch{}}
  return {suppression,gain,engine,captureOptions,save};
}
