import { ref } from 'vue';
import {Track, type LocalAudioTrack, type Room, type RoomEvent} from 'livekit-client';
import {useAudioLab} from './audioLab';
import {audioDiagnostic} from './audioDiagnostics';

export interface AudioDeviceOption { deviceId: string; label: string }
type AudioKind = 'audioinput' | 'audiooutput';
const activeDeviceChanged = 'activeDeviceChanged' as RoomEvent.ActiveDeviceChanged;
const defaults = {
  audioinput: { deviceId: 'default', label: 'По умолчанию (название недоступно)' },
  audiooutput: { deviceId: 'default', label: 'По умолчанию (название недоступно)' },
};

function defaultLabel(devices: MediaDeviceInfo[], kind: AudioKind): string {
  const alias = devices.find(device => device.kind === kind && device.deviceId === 'default');
  // Only the browser's default alias identifies the system choice; list order does not.
  const matches = alias?.groupId ? devices.filter(device => device.kind === kind && device.groupId === alias.groupId
    && device.deviceId !== 'default' && device.deviceId !== 'communications' && device.label) : [];
  const label = (matches.length === 1 ? matches[0]!.label : alias?.label || '')
    .replace(/^(?:default|по умолчанию|системный)\s*[-–—:]\s*/i, '').trim();
  return `По умолчанию (${label || 'название недоступно'})`;
}

function permissionMessage(error: unknown): string {
  const name = error instanceof Error ? error.name : '';
  if (name === 'NotAllowedError' || name === 'SecurityError') {
    return 'Доступ к микрофону запрещён. Разрешите его в настройках этого сайта и попробуйте снова.';
  }
  if (name === 'NotFoundError') return 'Микрофон не найден. Подключите устройство и обновите список.';
  if (name === 'NotReadableError') return 'Микрофон недоступен: проверьте, не занят ли он другой программой.';
  return 'Не удалось получить доступ к микрофону. Проверьте устройство и разрешения сайта.';
}

/** Device discovery is independent of room/config loading and never captures audio implicitly. */
export function useAudioDevices() {
  const lab=useAudioLab();
  const configured=new WeakMap<LocalAudioTrack,string>();
  const media = typeof navigator === 'undefined' ? undefined : navigator.mediaDevices;
  const supportsOutput = typeof HTMLMediaElement !== 'undefined'
    && typeof (HTMLMediaElement.prototype as HTMLMediaElement & { setSinkId?: unknown }).setSinkId === 'function';
  const inputs = ref<AudioDeviceOption[]>([{ ...defaults.audioinput }]);
  const outputs = ref<AudioDeviceOption[]>([{ ...defaults.audiooutput }]);
  const inputId = ref('default');
  const outputId = ref('default');
  const actualInput=ref('Микрофон не захватывается');
  try{inputId.value=localStorage.getItem('nimbo-input-device')||'default';outputId.value=localStorage.getItem('nimbo-output-device')||'default';}catch{}
  const cameraId=ref('default'),cameras=ref<AudioDeviceOption[]>([{deviceId:'default',label:'Камера по умолчанию'}]);
  const busy = ref(false);
  const note = ref('');
  let room: Room | null = null;
  let disposed = false;
  let refreshVersion = 0;
  let bindingVersion = 0;
  let pendingSwitches = 0;
  const switching: Record<AudioKind, number> = { audioinput: 0, audiooutput: 0 };
  const queues: Record<AudioKind, Promise<unknown>> = {
    audioinput: Promise.resolve(), audiooutput: Promise.resolve(),
  };

  function currentId(kind: AudioKind) { return kind === 'audioinput' ? inputId : outputId; }
  let activeListener:((kind:MediaDeviceKind,id:string)=>void)|undefined;
  function remember(){try{localStorage.setItem('nimbo-input-device',inputId.value);localStorage.setItem('nimbo-output-device',outputId.value);}catch{}}
  async function auditInput(event='microphone-state') {
    const current=room,track=current?.localParticipant?.getTrackPublication(Track.Source.Microphone)?.track;
    const raw=track?.mediaStreamTrack;
    const settings=(track as LocalAudioTrack|undefined)?.getSourceTrackSettings?.()||raw?.getSettings?.();
    if(current!==room)return;
    const label=inputs.value.find(d=>d.deviceId===settings?.deviceId)?.label;
    actualInput.value=raw?.readyState==='live'?(label||raw.label||'Микрофон активен'):'Микрофон не захватывается';
    audioDiagnostic(event,{selected:inputs.value.find(d=>d.deviceId===inputId.value)?.label||'Выбранное устройство',actual:actualInput.value,muted:track?.isMuted,readyState:raw?.readyState,echoCancellation:settings?.echoCancellation,noiseSuppression:settings?.noiseSuppression,autoGainControl:settings?.autoGainControl,sampleRate:settings?.sampleRate});
  }
  function bindRoom(next:Room|null){
    if(disposed)return;
    if(activeListener)room?.off(activeDeviceChanged,activeListener);
    bindingVersion++;room=next;
    // SDK events describe hardware state, not a new user preference. In particular,
    // a delayed event from an earlier capture must not overwrite the selected input.
    const version=bindingVersion;
    activeListener=()=>{if(version===bindingVersion&&!disposed)void auditInput('sdk-device-event');};
    room?.on(activeDeviceChanged,activeListener);
    void auditInput(next?'room-bound':'room-unbound');
  }
  function runInput<T>(operation:()=>Promise<T>):Promise<T|undefined>{
    const version=bindingVersion;pendingSwitches++;busy.value=true;
    const result=queues.audioinput.then(async()=>{if(disposed||version!==bindingVersion)return;return operation();});
    queues.audioinput=result.catch(()=>{});
    return result.finally(()=>{pendingSwitches--;if(!disposed)busy.value=pendingSwitches>0||refreshing;});
  }

  async function applyCaptureOptions(){
    const current=room,track=current?.localParticipant?.getTrackPublication(Track.Source.Microphone)?.track as LocalAudioTrack|undefined;
    if(!track||track.isMuted)return;
    const options=lab.captureOptions(),key=JSON.stringify(options);
    const constraints=track.constraints;
    if(configured.get(track)!==key&&(constraints.noiseSuppression!==options.noiseSuppression||constraints.autoGainControl!==options.autoGainControl)){
      await track.restartTrack({...options,deviceId:inputId.value==='default'?'default':{exact:inputId.value}});
      if(current!==room){track.stop();return;}
    }
    if(import.meta.env.VITE_AUDIO_LAB==='1'){
      const wants=lab.suppression.value&&lab.engine.value==='rnnoise';
      if(wants&&track.getProcessor()?.name!=='nimbo-rnnoise'){
        const {NimboNoiseProcessor}=await import('./rnnoiseProcessor');
        if(current!==room)return;
        const processor=new NimboNoiseProcessor();
        try{await track.setProcessor(processor);}catch(e){await processor.destroy();await track.restartTrack({...options,noiseSuppression:true});throw e;}
        if(current!==room){await track.stopProcessor();return;}
      }else if(!wants&&track.getProcessor()?.name==='nimbo-rnnoise')await track.stopProcessor();
    }
    configured.set(track,key);await auditInput('processing-applied');
  }
  async function updateProcessing(){
    lab.save();
    try{await runInput(applyCaptureOptions);note.value='';}
    catch{note.value='Не удалось применить обработку микрофона. Попробуйте ещё раз.';}
  }
  function select(kind: AudioKind, deviceId: string): Promise<boolean> {
    if (disposed) return Promise.resolve(false);
    if (kind === 'audiooutput' && !supportsOutput && deviceId !== 'default') {
      note.value = 'Этот браузер использует системный выход. Выберите наушники в настройках системы.';
      return Promise.resolve(false);
    }
    const targetRoom = room;
    const version = bindingVersion;
    // Serialize switches of the same kind: a slower earlier operation must not win on the hardware.
    pendingSwitches++;busy.value=true;
    const operation = queues[kind].then(async () => {
      if (disposed || version !== bindingVersion) return false;
      if (!targetRoom) { currentId(kind).value = deviceId;remember(); return true; }
      switching[kind]++;
      const before=currentId(kind).value;
      audioDiagnostic('switch-start',{kind,selected:(kind==='audioinput'?inputs:outputs).value.find(d=>d.deviceId===deviceId)?.label||'Выбранное устройство'});
      try {
        const changed = await targetRoom.switchActiveDevice(kind, deviceId);
        if (disposed || version !== bindingVersion) return false;
        const track=kind==='audioinput'?targetRoom.localParticipant?.getTrackPublication(Track.Source.Microphone)?.track:undefined;
        const actual=await track?.getDeviceId(false);
        if(disposed||version!==bindingVersion)return false;
        if ((!changed&&!(kind==='audioinput'&&deviceId==='default'&&actual))||(track&&!track.isMuted&&deviceId!=='default'&&actual!==deviceId)) throw new Error('Device switch mismatch');
        currentId(kind).value = deviceId;remember();
        if(kind==='audioinput'){await applyCaptureOptions();await auditInput('switch-complete');}
        note.value = '';
        return true;
      } catch {
        if (!disposed&&version===bindingVersion) {
          audioDiagnostic('switch-failed',{kind});
          try{await targetRoom.switchActiveDevice(kind,before);}catch{audioDiagnostic('switch-rollback-failed',{kind});}
          if(kind==='audioinput')await auditInput('switch-rollback');
        }
        if (!disposed && version === bindingVersion) {
          note.value = kind === 'audioinput'
            ? 'Не удалось переключить микрофон. Проверьте активный микрофон ниже и выберите устройство ещё раз.'
            : 'Не удалось переключить выход. Звонок продолжается; проверьте разрешения браузера или системный выход.';
        }
        return false;
      } finally {
        switching[kind]--;

      }
    });
    queues[kind] = operation.catch(()=>false);
    return operation.finally(()=>{pendingSwitches--;if(!disposed)busy.value=pendingSwitches>0||refreshing;});
  }

  let refreshing = false;
  async function refresh(requestPermission = false): Promise<void> {
    if (disposed) return;
    const version = ++refreshVersion;
    refreshing = true;
    busy.value = true;
    let stream: MediaStream | undefined;
    let permissionError = '';
    let permissionGranted = false;
    try {
      if (!media?.enumerateDevices) {
        note.value = 'Устройства недоступны. Откройте Nimbo через HTTPS или на localhost.';
        return;
      }
      if (requestPermission && !room?.localParticipant?.isMicrophoneEnabled) {
        try {
          stream = await media.getUserMedia({ audio: true, video: false });
          permissionGranted = true;
        } catch (error) {
          permissionError = permissionMessage(error);
        } finally {
          // This is only a permission probe. Never leave its microphone track running.
          stream?.getTracks().forEach((track) => track.stop());
        }
      }
      if (disposed || version !== refreshVersion) return;
      const devices = await media.enumerateDevices();
      if (disposed || version !== refreshVersion) return;
      cameras.value=[{deviceId:'default',label:'Камера по умолчанию'},...devices.filter(d=>d.kind==='videoinput'&&d.deviceId&&d.deviceId!=='default').map((d,i)=>({deviceId:d.deviceId,label:d.label||`Камера ${i+1}`}))];
      for (const kind of ['audioinput', 'audiooutput'] as const) {
        const seen = new Set(['default']);
        const options = [{ deviceId: 'default', label: defaultLabel(devices, kind) }];
        for (const device of devices) {
          if (device.kind !== kind || !device.deviceId || seen.has(device.deviceId)) continue;
          seen.add(device.deviceId);
          options.push({ deviceId: device.deviceId, label: device.label || `${kind === 'audioinput' ? 'Микрофон' : 'Выход'} ${options.length}` });
        }
        const selected = currentId(kind).value;
        const hasVisibleLabels = devices.some((device) => device.label);
        if (selected !== 'default' && !seen.has(selected)) {
          if (!permissionError && (permissionGranted || hasVisibleLabels)) {
            // Reconcile both the real room device and UI; a failed switch keeps the previous choice visible.
            await select(kind, 'default');
            if (disposed || version !== refreshVersion) return;
          }
          if (currentId(kind).value !== 'default') {
            options.push({ deviceId: currentId(kind).value, label: 'Выбранное устройство недоступно' });
          }
        }
        (kind === 'audioinput' ? inputs : outputs).value = options;
      }
      if (permissionError) note.value = permissionError;
      else if (!devices.some((device) => device.kind === 'audioinput')) {
        note.value = permissionGranted
          ? 'Микрофон не найден. Подключите устройство и обновите список.'
          : 'Микрофоны пока не видны. Нажмите «Обновить список устройств» для доступа к списку.';
      } else if (!devices.some((device) => device.kind === 'audioinput' && device.label)) {
        note.value = 'Браузер скрывает названия устройств. Нажмите «Обновить список устройств».';
      } else if (!supportsOutput) {
        note.value = 'В этом браузере выход выбирается в настройках системы.';
      } else if (!note.value.includes('Не удалось переключить')) note.value = '';
    } catch {
      if (!disposed && version === refreshVersion) {
        note.value = permissionError || 'Не удалось прочитать список устройств. Попробуйте обновить его ещё раз.';
      }
    } finally {
      if (!disposed && version === refreshVersion) {
        refreshing = false;
        busy.value = pendingSwitches > 0;
      }
    }
  }

  const onDeviceChange = () => { void refresh(); };
  media?.addEventListener?.('devicechange', onDeviceChange);
  function dispose() {
    if (disposed) return;
    if(activeListener)room?.off(activeDeviceChanged,activeListener);
    room = null;
    disposed = true;
    refreshVersion++;
    bindingVersion++;
    busy.value = false;
    media?.removeEventListener?.('devicechange', onDeviceChange);
  }

  return {
    lab,captureOptions:lab.captureOptions,applyCaptureOptions,updateProcessing,cameraId,cameras,actualInput,runInput,auditInput,
    inputs, outputs, inputId, outputId, busy, note, supportsOutput, refresh,
    selectInput: (id: string) => select('audioinput', id),
    selectOutput: (id: string) => select('audiooutput', id), bindRoom, dispose,
  };
}
