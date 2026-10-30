import {ref} from 'vue';
import type {LocalVideoTrack} from 'livekit-client';
import type {BackgroundProcessorWrapper} from '@livekit/track-processors';
export function useCameraEffects(current:()=>LocalVideoTrack|undefined){
 const enabled=ref(false),strength=ref(12),busy=ref(false),note=ref('');
 async function apply(){
  const track=current();if(!track||busy.value)return;busy.value=true;note.value='';
  let created:BackgroundProcessorWrapper|undefined;
  const existing=track.getProcessor() as BackgroundProcessorWrapper|undefined;
  const previousEnabled=existing?.mode==='background-blur';
  try{
   // Keep the published track and preview srcObject stable. Disabled mode
   // passes original frames through without segmentation.
   if(existing){await existing.switchTo(enabled.value?{mode:'background-blur',blurRadius:strength.value}:{mode:'disabled'});return;}
   const {BackgroundProcessor,supportsBackgroundProcessors}=await import('@livekit/track-processors');
   if(current()!==track)return;
   if(!supportsBackgroundProcessors()){if(enabled.value)throw new Error('unsupported');return;}
   // Install once at camera startup, even without effects. Later selections
   // only switch mode and never replace the video stream.
   created=BackgroundProcessor({mode:enabled.value?'background-blur':'disabled',blurRadius:strength.value,assetPaths:{tasksVisionFileSet:'/effects/wasm',modelAssetPath:'/effects/selfie_segmenter.tflite'}});
   await track.setProcessor(created);
   if(current()!==track&&track.getProcessor()===created)await track.stopProcessor();
  }catch{
   if(current()===track){enabled.value=previousEnabled;note.value='Не удалось изменить эффект в этом браузере. Камера продолжает работать.';}
   // A failed mode switch must not tear down the working camera pipeline.
   if(created){try{if(track.getProcessor()===created)await track.stopProcessor();else await created.destroy();}catch{/* camera lifecycle may have ended */}}
  }
  finally{busy.value=false;}
 }
 async function toggle(){if(busy.value)return;enabled.value=!enabled.value;await apply();}
 async function setStrength(value:number){if(!Number.isFinite(value)||busy.value)return;strength.value=Math.max(1,Math.min(60,Math.round(value)));await apply();}
 return {enabled,strength,busy,note,apply,toggle,setStrength};
}
