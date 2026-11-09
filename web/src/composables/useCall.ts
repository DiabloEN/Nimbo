import {apiFetch} from './serverConnection';
import { computed, ref } from 'vue';
import { Room, RoomEvent, Track, VideoPresets, AudioPresets, setLogLevel } from 'livekit-client';
import type { Participant, LocalParticipant, LocalTrackPublication, LocalVideoTrack, RemoteTrackPublication, VideoCaptureOptions } from 'livekit-client';
import { createVideoLimiter, applyReceiveLimit, videoModes, type SendQuality, type ReceiveQuality } from './videoQuality';
import {useCameraEffects} from './useCameraEffects';

import {audioDiagnostic} from './audioDiagnostics';
import {createSpeakingMeter} from './speakingMeter';
import joinSound from '../assets/sounds/voice-join.wav';
import leaveSound from '../assets/sounds/voice-leave.wav';

import muteSound from '../assets/sounds/voice-mute.wav';
import unmuteSound from '../assets/sounds/voice-unmute.wav';
import videoOnSound from '../assets/sounds/voice-video-on.wav';
import videoOffSound from '../assets/sounds/voice-video-off.wav';

type MediaKind = 'mic' | 'camera' | 'screen';
function mediaState(peer: Participant) {
  return {mic: !!peer.isMicrophoneEnabled, camera: !!peer.isCameraEnabled, screen: !!peer.isScreenShareEnabled};
}

export type VideoTile = { id:string; participantId:string; name:string; source:'camera'|'screen'; local:boolean; aspectRatio?:number;streamId?:string;displaySurface?:string };
type MediaTrack = Track;
const screenUnavailable = 'Этот браузер не поддерживает показ своего экрана. Вы можете смотреть экран собеседника, а для своей демонстрации подключиться с компьютера.';
type Devices = {
  runInput?:<T>(op:()=>Promise<T>)=>Promise<T|undefined>;
  applyCaptureOptions?:()=>Promise<void>;
  auditInput?:(event?:string)=>Promise<void>;
  captureOptions?:()=>{echoCancellation:boolean;noiseSuppression:boolean;autoGainControl:boolean};
  cameraId?: {value:string};
  inputId: { value: string }; outputId: { value: string }; supportsOutput: boolean;
  bindRoom: (room: Room | null) => void;
  selectOutput: (id: string) => Promise<boolean>;
  refresh: (permission?: boolean) => Promise<void>;
};

// SDK rooms/tracks stay outside Vue's reactive graph. Only UI snapshots are reactive.
export function useCall(devices: Devices, media: () => { videos: HTMLElement | null; audio: HTMLElement | null }) {
  setLogLevel('warn');
  const serialInput=<T>(op:()=>Promise<T>)=>devices.runInput?devices.runInput(op):op();
  const micOptions=()=>({deviceId:devices.inputId.value==='default'?'default':{exact:devices.inputId.value},...(devices.captureOptions?.()||{echoCancellation:true,noiseSuppression:true,autoGainControl:true})});
  let room: Room | null = null;
  async function playCue(active: Room, url: string) {
    try {
      if (room !== active || active.state !== 'connected') return;
      const cue = new Audio(url);
      const output = devices.outputId.value;
      if (output && output !== 'default' && cue.setSinkId) await cue.setSinkId(output);
      if (room === active && active.state === 'connected') await cue.play();
    } catch { /* Optional feedback must not interrupt the call. */ }
  }
  function mediaCue(active: Room, kind: MediaKind, enabled: boolean) {
    const sound = kind === 'mic' ? (enabled ? unmuteSound : muteSound)
      : (enabled ? videoOnSound : videoOffSound);
    void playCue(active, sound);
  }
  const effects=useCameraEffects(()=>room?.localParticipant.getTrackPublication(Track.Source.Camera)?.track as LocalVideoTrack|undefined);
  const volumes=ref<Record<string,number>>({});
  try{const saved=JSON.parse(localStorage.getItem('nimbo-participant-volumes')||'{}');for(const [key,value]of Object.entries(saved)){if(typeof value==='number'&&Number.isFinite(value))volumes.value[key]=Math.max(0,Math.min(200,value));}}catch{/* optional local preferences */}
  const audioOwners=new Map<MediaTrack,string>();
  const volumeKey=(id:string)=>(id||'').split('.')[0]!;
  const volumeFor=(id:string)=>volumes.value[volumeKey(id)]??100;
  function setParticipantVolume(id:string,value:number){
    if(!Number.isFinite(value))return;const amount=Math.max(0,Math.min(200,value));volumes.value[volumeKey(id)]=amount;
    for(const [track,owner]of audioOwners)if(volumeKey(owner)===volumeKey(id)) (track as MediaTrack&{setVolume?:(n:number)=>void}).setVolume?.(amount/100);
    try{localStorage.setItem('nimbo-participant-volumes',JSON.stringify(volumes.value));}catch{/* optional local preferences */}
  }
  async function selectCamera(id:string){if(!devices.cameraId||pending.value||effects.busy.value)return;const before=devices.cameraId.value;pending.value='camera-device';try{if(room&&!await room.switchActiveDevice('videoinput',id))throw new Error();devices.cameraId.value=id;await devices.refresh();}catch{devices.cameraId.value=before;status.value='Не удалось переключить камеру. Сохранён предыдущий выбор.';}finally{pending.value='';}}
  let generation = 0;
  let abortJoin: AbortController | null = null;
  const attachments = new Map<MediaTrack, { element: HTMLMediaElement; container: HTMLElement; publication?: RemoteTrackPublication; tile?: VideoTile; cleanup?:()=>void }>();
  const videoHosts = new Map<string, HTMLElement>();
  const videoTiles = ref<VideoTile[]>([]);
  let tileCounter = 0;
  const limiter = createVideoLimiter();
  let qualityTimer: ReturnType<typeof setInterval> | undefined;
  const linkQuality=ref('unknown');
  const joining = ref(false);
  const connected = ref(false);
  const hasRoom = ref(false);
  const status = ref('Введите имя и код доступа, чтобы присоединиться.');
  const mic = ref(false), camera = ref(false), screen = ref(false), needsAudio = ref(false);
  const screenAudio = ref(false), screenAudioAvailable = ref(false), screenAudioNote = ref('');
  const pending = ref('');
  const supportsScreen = typeof navigator !== 'undefined' && typeof navigator.mediaDevices?.getDisplayMedia === 'function';
  const sendQuality = ref<SendQuality>('auto');
  const receiveQuality = ref<ReceiveQuality>('auto');
  const qualityNote = ref('');
  const videoCount = ref(0);
  function syncVideoTiles() {
    videoTiles.value = [...attachments.values()].flatMap(item => item.tile ? [{...item.tile}] : []);
    videoCount.value = videoTiles.value.length;
  }
  function bindVideoHost(id:string, host:HTMLElement|null) {
    if(host)videoHosts.set(id,host);else videoHosts.delete(id);
    const item=[...attachments.values()].find(item=>item.tile?.id===id);
    const target=host||media().videos;
    if(item&&target&&item.container.parentElement!==target)target.append(item.container);
    applyReceive();
  }
  function cameraCapture(): VideoCaptureOptions {
    // Capture stays alive at the same resolution; limits apply to transmitted RTP.
    return { deviceId:devices.cameraId?.value||'default',resolution: { width: 1920, height: 1080 }, frameRate: { ideal: 30, max: 30 } };
  }
  async function applySend(current: Room) {
    for (const publication of current.localParticipant.videoTrackPublications?.values() ?? []) {
      if (publication.track) await limiter.apply(publication.track as LocalVideoTrack, sendQuality.value, () => room === current);
    }
  }
  function applyReceive() {
    const hidden = typeof document !== 'undefined' && document.visibilityState === 'hidden';
    for (const item of attachments.values()) {
      if (!item.publication) continue;
      item.container.hidden = receiveQuality.value === 'audio';
      const rect = item.element.getBoundingClientRect();
      const density = Math.min(window.devicePixelRatio || 1, 2);
      applyReceiveLimit(item.publication, receiveQuality.value, { width: rect.width * density,
        height: rect.height * density, visible: rect.width > 0 && rect.height > 0
          && rect.bottom > 0 && rect.top < window.innerHeight }, hidden);
    }
  }
  const participants = ref<{ identity: string; name: string; local: boolean; speaking: boolean; mic: boolean; micKnown:boolean;camera:boolean;screen:boolean }[]>([]);
  let localSpeaking:boolean|undefined;
  const speakingMeter=createSpeakingMeter(()=>room?.localParticipant.isMicrophoneEnabled?room.localParticipant.getTrackPublication(Track.Source.Microphone)?.track?.mediaStreamTrack:undefined,value=>{
    localSpeaking=value;
    participants.value=participants.value.map(p=>p.local?{...p,speaking:mic.value&&(value??!!room?.localParticipant.isSpeaking)}:p);
  });
  const connection = computed(() => connected.value ? 'Вы в комнате' : joining.value ? 'Подключение…' : hasRoom.value ? 'Восстанавливаем связь…' : 'Не подключено');
  function sync() {
    connected.value = room?.state === 'connected'; hasRoom.value = !!room;
    mic.value = !!room?.localParticipant.isMicrophoneEnabled;
    camera.value = !!room?.localParticipant.isCameraEnabled;
    screen.value = !!room?.localParticipant.isScreenShareEnabled;
    const sharedAudio = room?.localParticipant.getTrackPublication(Track.Source.ScreenShareAudio);
    screenAudioAvailable.value = !!sharedAudio?.track && sharedAudio.track.mediaStreamTrack.readyState === 'live';
    screenAudio.value = screen.value && screenAudioAvailable.value && !sharedAudio?.isMuted;
    if (!screen.value) screenAudioNote.value = '';
    participants.value = room ? [room.localParticipant, ...room.remoteParticipants.values()].map(p => ({
      identity: p.identity, name: p.name || 'Гость', local: p === room!.localParticipant, speaking: p.isMicrophoneEnabled&&(p===room!.localParticipant?(localSpeaking??p.isSpeaking):p.isSpeaking), mic: p.isMicrophoneEnabled, micKnown:true,camera:p.isCameraEnabled,screen:p.isScreenShareEnabled,
    })) : [];
  }
  function removeTrack(track: MediaTrack) {
    audioOwners.delete(track);
    const item = attachments.get(track);
    if (item) { item.cleanup?.(); track.detach(item.element); item.container.remove(); attachments.delete(track); if(item.tile)videoHosts.delete(item.tile.id); syncVideoTiles(); }
  }
  function attachTrack(track: MediaTrack, participant: Participant, local = false, publication?: RemoteTrackPublication) {
    if (attachments.has(track) || (local && track.kind === 'audio') || (track.kind === 'video' && track.isMuted)) return;
    const element = track.attach(); element.autoplay = true;
    let container: HTMLElement = element;
    let tile:VideoTile|undefined;
    if (track.kind === 'video') {
      tile={id:'media-'+(++tileCounter),participantId:participant.identity,name:participant.name||'Гость',source:track.source==='screen_share'?'screen':'camera',local,streamId:track.sid,displaySurface:local?track.mediaStreamTrack?.getSettings?.().displaySurface:undefined};
      (element as HTMLVideoElement).playsInline = true; element.muted = local;
      container = document.createElement('figure'); container.className = 'video-tile';
      if(local&&tile.source==='camera')element.className=(element.className||'')+' local-camera-preview';
      const caption = document.createElement('figcaption'); caption.textContent = participant.name || 'Гость';
      container.append(element, caption); media().videos?.append(container);
    } else { media().audio?.append(element);audioOwners.set(track,participant.identity);(track as MediaTrack&{setVolume?:(n:number)=>void}).setVolume?.(volumeFor(participant.identity)/100); }
    attachments.set(track, { element, container, publication: track.kind === 'video' ? publication : undefined, tile });
    if(tile){
      const video=element as HTMLVideoElement;
      const updateSize=()=>{
        if(!attachments.has(track)||!video.videoWidth||!video.videoHeight)return;
        const ratio=video.videoWidth/video.videoHeight;
        if(tile.aspectRatio!==ratio){tile.aspectRatio=ratio;syncVideoTiles();}
      };
      video.addEventListener('loadedmetadata',updateSize);
      video.addEventListener('resize',updateSize);
      attachments.get(track)!.cleanup=()=>{
        video.removeEventListener('loadedmetadata',updateSize);
        video.removeEventListener('resize',updateSize);
      };
      updateSize();
    }
    syncVideoTiles();
    // LiveKit applies the selected output to its remote audio tracks.
    element.play().catch(() => { if (track.kind==='audio'&&attachments.has(track)) needsAudio.value = true; });
  }
  async function leave(message = 'Вы вышли из комнаты.') {

    audioDiagnostic('call-leave');
    speakingMeter.stop();
    linkQuality.value='unknown';
    ++generation; abortJoin?.abort(); abortJoin = null; joining.value = false;
    clearInterval(qualityTimer); qualityTimer = undefined;
    const previous = room; room = null; devices.bindRoom(null);
    for (const track of [...attachments.keys()]) removeTrack(track);
    pending.value = ''; needsAudio.value = false; sync(); status.value = message;
    if (previous) await previous.disconnect(true);
  }
  function playAudio() {
    const current = room;
    if (!current) return;
    // resume() can wait for a user gesture indefinitely; never block join on it.
    void current.startAudio().then(() => {
      if (room === current) needsAudio.value = !current.canPlaybackAudio;
    }).catch(() => { if (room === current) needsAudio.value = true; });
  }
  async function join(form: { name: string; password: string; room: string; withMic: boolean; session?: string }) {
    if (room || joining.value) return;
    if (!window.isSecureContext) { status.value = 'Для подключения нужен HTTPS или localhost.'; return; }
    const current = ++generation;
    joining.value = true; status.value = 'Подключаемся к комнате…';
    const controller = new AbortController(); abortJoin = controller;
    const timeout = setTimeout(() => controller.abort(), 15000);
    let candidate: Room | undefined;
    try {
      const response = await apiFetch('/api/join', { method: 'POST', headers: { 'Content-Type': 'application/json', ...(form.session ? { Authorization: `Bearer ${form.session}` } : {}) }, signal: controller.signal,
        body: JSON.stringify({ name: form.name.trim(), password: form.password, room: form.room }) });
      const invitation = await response.json();
      clearTimeout(timeout);
      if (current !== generation) return;
      if (!response.ok) { status.value = invitation.error || 'Вход недоступен.'; return; }
      status.value = 'Вход подтверждён. Устанавливаем медиасоединение…';
      // Subscription limits below handle visibility and size as well as manual caps.
      candidate = new Room({ disconnectOnPageLeave:false, adaptiveStream: false, dynacast: true, webAudioMix:true,
        publishDefaults: { videoSimulcastLayers: [VideoPresets.h360, VideoPresets.h720],
          screenShareSimulcastLayers: [VideoPresets.h360, VideoPresets.h720] },
        audioCaptureDefaults: micOptions() });
      const active = candidate;
      room = active; sync();
      active.on(RoomEvent.TrackSubscribed, (track, publication, participant) => {
        if (room === active && !(track.kind === 'video' && publication.isMuted)) { attachTrack(track, participant, false, publication); applyReceive(); }
      });
      active.on(RoomEvent.TrackUnsubscribed, removeTrack);
      active.on(RoomEvent.LocalTrackPublished, (publication, participant) => {
        if (room !== active) return;
        trackMedia(participant, publication.source, !publication.isMuted);
        if (publication.track) attachTrack(publication.track, participant, true); sync();
        void applySend(active).catch(() => { if (room === active) qualityNote.value = 'Не удалось применить ограничение отправки. Выберите режим ещё раз.'; });
      });
      active.on(RoomEvent.LocalTrackUnpublished, publication => {
        trackMedia(active.localParticipant, publication.source, false);
        if (publication.track) removeTrack(publication.track); if (room === active) sync();
        if (publication.source === Track.Source.ScreenShare) {
          const audio = active.localParticipant.getTrackPublication(Track.Source.ScreenShareAudio)?.track;
          if (audio) void active.localParticipant.unpublishTrack(audio).catch(() => audio.stop());
        }
      });
      const peerMedia = new Map<string, ReturnType<typeof mediaState>>();
      function trackMedia(peer: Participant, source: Track.Source, enabled: boolean) {
        if (room !== active || active.state !== 'connected' || !peers) return;
        if (peer === active.localParticipant) {
          // Local microphone feedback follows the user's toggle, including hotkeys.
          if (source === Track.Source.Microphone) return;
        } else if (active.remoteParticipants.get(peer.identity) !== peer) return;
        const state = peerMedia.get(peer.identity);
        const kind = source === Track.Source.Microphone ? 'mic' : source === Track.Source.Camera ? 'camera'
          : source === Track.Source.ScreenShare ? 'screen' : undefined;
        if (!state || !kind || state[kind] === enabled) return;
        state[kind] = enabled;
        mediaCue(active, kind, enabled);
      }
      active.on(RoomEvent.TrackPublished, (pub, peer) => trackMedia(peer, pub.source, !pub.isMuted));
      active.on(RoomEvent.TrackUnpublished, (pub, peer) => trackMedia(peer, pub.source, false));
      // Camera disable mutes a publication; it need not unpublish/unsubscribe it.
      active.on(RoomEvent.TrackMuted, (publication, participant) => {
        if (room !== active) return;
        if (participant) trackMedia(participant, publication.source, false);
        if (publication.track?.kind === 'video') removeTrack(publication.track);
        if (publication === active.localParticipant.getTrackPublication(Track.Source.ScreenShare)) {
          const audio = active.localParticipant.getTrackPublication(Track.Source.ScreenShareAudio);
          if (audio) void audio.mute().catch(() => audio.track?.stop());
        }
        sync();
      });
      active.on(RoomEvent.TrackUnmuted, (publication, participant) => {
        if (room !== active) return;
        trackMedia(participant, publication.source, true);
        if (publication.track?.kind === 'video') attachTrack(publication.track, participant, participant === active.localParticipant,
          participant === active.localParticipant ? undefined : publication as RemoteTrackPublication);
        sync();
      });
      let peers: Set<string> | undefined;
      const seedPeers = () => {
        peers = new Set(active.remoteParticipants.keys());
        peerMedia.clear();
        peerMedia.set(active.localParticipant.identity, mediaState(active.localParticipant));
        for (const [id, peer] of active.remoteParticipants) peerMedia.set(id, mediaState(peer));
      };
      async function peerChanged(peer: Participant, arrived: boolean) {
        if (room !== active) return;
        sync();
        if (!peers || active.state !== 'connected' || peers.has(peer.identity) === arrived) return;
        if (arrived) {
          peers.add(peer.identity);
          peerMedia.set(peer.identity, mediaState(peer));
        } else {
          peers.delete(peer.identity);
          peerMedia.delete(peer.identity);
        }
        await playCue(active, arrived ? joinSound : leaveSound);
      }
      active.on(RoomEvent.ParticipantConnected, peer => { void peerChanged(peer, true); });
      active.on(RoomEvent.ParticipantDisconnected, peer => { void peerChanged(peer, false); });
      active.on(RoomEvent.ActiveSpeakersChanged, () => { if (room === active) sync(); });
      active.on(RoomEvent.ConnectionQualityChanged,(quality,participant)=>{if(room===active&&participant===active.localParticipant)linkQuality.value=quality;});
      active.on(RoomEvent.Reconnecting, () => { if (room === active) { peers = undefined; sync(); status.value = 'Соединение потеряно. Пробуем восстановить…'; } });
      active.on(RoomEvent.Reconnected, () => { if (room === active) { seedPeers(); sync(); status.value = 'Связь восстановлена.'; } });
      active.on(RoomEvent.Disconnected, () => { if (room === active) void leave(joining.value
        ? 'Вход подтверждён, но медиасоединение не установлено. Проверьте доступность медиасервера.'
        : 'Связь завершена. Можно войти снова.'); });
      active.on(RoomEvent.AudioPlaybackStatusChanged, () => { if (room === active) needsAudio.value = !active.canPlaybackAudio; });
      await active.connect(invitation.url, invitation.token, { autoSubscribe: true, peerConnectionTimeout: 15000, websocketTimeout: 10000 });
      if (current !== generation) { await active.disconnect(true); return; }
      seedPeers();
      speakingMeter.start();
      qualityTimer = setInterval(() => {
        if (room !== active) return;
        applyReceive();
        void applySend(active).catch(() => { if (room === active) qualityNote.value = 'Браузер не применил ограничение отправки. Повторите выбор режима.'; });
      }, 2000);
      const input = devices.inputId.value, output = devices.outputId.value;
      devices.bindRoom(active);
      // With no published microphone this is still the user's pending device choice.
      devices.inputId.value = input;
      // Output failures are recoverable and must not tear down the call.
      const outputOK = !devices.supportsOutput || await devices.selectOutput(output);
      if (current !== generation) { await active.disconnect(true); return; }
      needsAudio.value = !active.canPlaybackAudio; playAudio();
      let micOK = true;
      if (form.withMic) {
        try {
          const publication = await serialInput(async()=>{if(current!==generation)return;const result=await active.localParticipant.setMicrophoneEnabled(true,micOptions());if(current===generation){await devices.applyCaptureOptions?.();await devices.auditInput?.('join-microphone');}return result;});
          if (current !== generation) publication?.track?.stop();
        }
        catch { micOK = false; }
      }
      if (current !== generation) { await active.disconnect(true); return; }
      sync();
      status.value = !micOK ? 'Вы подключены без микрофона: проверьте разрешение и выбранное устройство.'
        : !outputOK ? 'Вы подключены. Выход звука не переключился — проверьте устройства в настройках.'
        : 'Вы в комнате.';
      void devices.refresh();
    } catch {
      // SDK errors can contain token-bearing URLs; do not log them.
      if (current === generation) await leave('Не удалось подключиться. Проверьте доступность сервера и попробуйте снова.');
      else if (candidate) await candidate.disconnect(true);
    } finally {
      clearTimeout(timeout);
      if (current === generation) { joining.value = false; abortJoin = null; sync(); }
    }
  }
  async function toggle(kind: 'mic' | 'camera' | 'screen') {
    const current = room;
    if (!current || current.state !== 'connected' || pending.value || effects.busy.value) return;
    if (kind === 'screen' && !supportsScreen && !current.localParticipant.isScreenShareEnabled) {
      status.value = screenUnavailable; return;
    }
    pending.value = kind;
    const participant: LocalParticipant = current.localParticipant;
    const wasEnabled = mediaState(participant)[kind];
    try {
      let publication: LocalTrackPublication | undefined;
      if (kind === 'mic') publication = await serialInput(async()=>{if(room!==current)return;const result=await participant.setMicrophoneEnabled(!participant.isMicrophoneEnabled,micOptions());if(room===current){await devices.applyCaptureOptions?.();await devices.auditInput?.('toggle-microphone');}return result;});
      else if (kind === 'camera') publication = await participant.setCameraEnabled(!participant.isCameraEnabled, cameraCapture());
      else {
        screenAudioNote.value = '';
        publication = await participant.setScreenShareEnabled(!participant.isScreenShareEnabled, { audio: {echoCancellation:false,noiseSuppression:false,autoGainControl:false}, systemAudio: 'include' }, {audioPreset:AudioPresets.musicHighQualityStereo,dtx:false});
      }
      // A permission dialog can finish after leave(). Release that late capture too.
      if (room !== current) {
        publication?.track?.stop();
        if (kind === 'screen') await participant.setScreenShareEnabled(false);
      }
      const enabled = mediaState(participant)[kind];
      if (room === current && kind === 'mic' && enabled !== wasEnabled) mediaCue(current, kind, enabled);
      if(room===current&&kind==='camera'&&participant.isCameraEnabled)await effects.apply();
      if (room === current && kind !== 'mic') await applySend(current).catch(() => { if (room === current) qualityNote.value = 'Камера работает, но браузер не применил ограничение отправки. Повторите выбор режима.'; });
      if (room === current && (kind === 'mic'||kind==='camera')) void devices.refresh();
      if (room === current) {
        status.value = 'Вы в комнате.';
        if (kind === 'screen' && participant.isScreenShareEnabled && !participant.getTrackPublication(Track.Source.ScreenShareAudio)?.track) screenAudioNote.value = 'Браузер передал экран без звука. Разрешите звук при следующем запуске демонстрации.';
      }
    } catch (error) { if (room === current) status.value = captureError(kind, error); }
    finally { if (room === current) { pending.value = ''; sync(); } }
  }
  async function toggleScreenAudio() {
    const current = room;
    if (!current || current.state !== 'connected' || pending.value || !screen.value) return;
    const publication = current.localParticipant.getTrackPublication(Track.Source.ScreenShareAudio);
    if (!publication?.track || publication.track.mediaStreamTrack.readyState !== 'live') return;
    pending.value = 'screen-audio'; screenAudioNote.value = '';
    try {
      if (publication.isMuted) await publication.unmute(); else await publication.mute();
    } catch {
      if (room === current) screenAudioNote.value = 'Не удалось переключить звук демонстрации. Попробуйте ещё раз.';
    } finally { if (room === current) { pending.value = ''; sync(); } }
  }
  async function selectSendQuality(value: string) {
    if (!Object.hasOwn(videoModes, value) || pending.value || joining.value) return;
    const previous = sendQuality.value;
    const current = room;
    if (current && current.state !== 'connected') return;
    sendQuality.value = value as SendQuality;
    if (!current) return;
    pending.value = 'send-quality';
    try {
      await applySend(current);
      if (room === current) qualityNote.value = '';
    } catch {
      if (room === current) {
        sendQuality.value = previous;
        try { await applySend(current); } catch { /* leave the live track intact */ }
        qualityNote.value = 'Браузер не смог изменить параметры видео. Попробуйте ещё раз; звонок продолжается.';
      }
    } finally { if (room === current) { pending.value = ''; sync(); } }
  }
  function selectReceiveQuality(value: string) {
    if (value !== 'audio' && !Object.hasOwn(videoModes, value)) return;
    receiveQuality.value = value as ReceiveQuality;
    applyReceive();
  }
  async function selectQuality(value: string) {
    await selectSendQuality(value);
    // Keep both directions on the successfully applied mode, including rollback.
    selectReceiveQuality(sendQuality.value);
  }
  return { linkQuality,effects,selectCamera,volumeFor,setParticipantVolume,videoTiles, bindVideoHost, refreshVideoLayout:applyReceive, videoCount, selectQuality, joining, connected, hasRoom, status, mic, camera, screen, needsAudio, pending, participants, connection,
    screenAudio, screenAudioAvailable, screenAudioNote, toggleScreenAudio,
    supportsScreen, screenUnavailable, sendQuality, receiveQuality, qualityNote, selectSendQuality, selectReceiveQuality, join, leave, toggle, playAudio };
}

function captureError(kind: 'mic' | 'camera' | 'screen', error: unknown): string {
  const name = error && typeof error === 'object' && 'name' in error ? String(error.name) : '';
  const device = kind === 'camera' ? 'камере' : kind === 'mic' ? 'микрофону' : 'экрану';
  if (kind === 'screen' && name === 'NotSupportedError') return screenUnavailable;
  let message: string;
  if (name === 'NotAllowedError' || name === 'SecurityError') {
    message = `Доступ к ${device} не разрешён или запрос отменён. Проверьте разрешение сайта и приложения в настройках конфиденциальности системы.`;
  } else if (name === 'NotReadableError' || name === 'TrackStartError' || name === 'AbortError') {
    message = `Не удалось запустить захват: устройство может быть занято другим браузером или приложением. Выключите его там и попробуйте снова.`;
  } else if (name === 'NotFoundError' || name === 'DevicesNotFoundError') {
    message = kind === 'camera' ? 'Камера не найдена. Проверьте её подключение.' : 'Устройство захвата не найдено. Проверьте подключение.';
  } else if (name === 'OverconstrainedError') {
    message = 'Устройство не поддерживает запрошенные параметры захвата.';
  } else {
    message = `Не удалось включить захват. Попробуйте ещё раз или откройте Nimbo в Chrome.`;
  }
  // Show a bounded standard code, never raw errors containing URLs or tokens.
  const known = ['NotAllowedError', 'SecurityError', 'NotReadableError', 'TrackStartError', 'AbortError', 'NotFoundError', 'DevicesNotFoundError', 'OverconstrainedError'];
  return `${message}${known.includes(name) ? ` (${name})` : ''} Звонок продолжается.`;
}
