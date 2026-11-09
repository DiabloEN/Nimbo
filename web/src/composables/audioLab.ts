import {ref} from 'vue';
export const audioLabEnabled=import.meta.env.VITE_AUDIO_LAB!=='0';
const localNoiseEngines = ['gtcrn', 'rnnoise'] as const;
export type LocalNoiseEngine = typeof localNoiseEngines[number];
const defaultEngine: LocalNoiseEngine = 'gtcrn';
export function isLocalNoiseEngine(value: unknown): value is LocalNoiseEngine {
  return localNoiseEngines.some(engine => engine === value);
}
export function useAudioLab() {
  const suppression = ref(true);
  const engine = ref<'browser' | LocalNoiseEngine>(defaultEngine);
  if (audioLabEnabled) {
    try {
      const saved = JSON.parse(localStorage.getItem('nimbo-audio-lab') || '{}');
      suppression.value = saved.suppression !== false;
      if (saved.engine === 'browser' || isLocalNoiseEngine(saved.engine)) engine.value = saved.engine;
    } catch { /* Settings may be unavailable in this browser. */ }
  }
  const captureOptions = () => ({
    echoCancellation: true,
    noiseSuppression: audioLabEnabled ? suppression.value && engine.value === 'browser' : true,
    autoGainControl: false,
  });
  function save() {
    if (audioLabEnabled) {
      try {
        localStorage.setItem('nimbo-audio-lab', JSON.stringify({suppression: suppression.value, engine: engine.value}));
      } catch { /* Keep the choice for this session when storage is unavailable. */ }
    }
  }
  return {suppression, engine, captureOptions, save};
}
