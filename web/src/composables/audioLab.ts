import {ref} from 'vue';
export const audioLabEnabled=import.meta.env.VITE_AUDIO_LAB==='1';
export function useAudioLab(){
  const suppression=ref(true),gain=ref(true),engine=ref<'browser'|'rnnoise'>('rnnoise');
  if(audioLabEnabled)try{const p=JSON.parse(localStorage.getItem('nimbo-audio-lab')||'{}');suppression.value=p.suppression!==false;gain.value=p.gain!==false;engine.value=p.engine==='browser'?'browser':'rnnoise';}catch{}
  const captureOptions=()=>({echoCancellation:true,noiseSuppression:audioLabEnabled?suppression.value&&engine.value==='browser':true,autoGainControl:audioLabEnabled?gain.value:true});
  function save(){if(audioLabEnabled)try{localStorage.setItem('nimbo-audio-lab',JSON.stringify({suppression:suppression.value,gain:gain.value,engine:engine.value}));}catch{}}
  return {suppression,gain,engine,captureOptions,save};
}
