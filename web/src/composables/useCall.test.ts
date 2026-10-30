import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { useCall } from './useCall';

const sdk = vi.hoisted(() => {
  class MockRoom {
    static instances: MockRoom[] = [];
    state = 'disconnected';
    canPlaybackAudio = true;
    remoteParticipants = new Map();
    options: { audioCaptureDefaults?: { deviceId?: string } };
    localParticipant = {
      identity: 'local', name: 'Tester', isSpeaking: false,
      isMicrophoneEnabled: false, isCameraEnabled: false, isScreenShareEnabled: false,
      setMicrophoneEnabled: vi.fn(async (_enabled: boolean, _options?: { deviceId?: string }) => undefined as unknown),
      setCameraEnabled: vi.fn(async (_enabled: boolean, _options?: unknown) => undefined as unknown),
      setScreenShareEnabled: vi.fn(async () => undefined),
      getTrackPublication: vi.fn(() => undefined as { track: unknown } | undefined),
      unpublishTrack: vi.fn(async (_track: unknown, _stop: boolean) => undefined),
      videoTrackPublications: new Map<string, { track: any }>(),
    };
    handlers = new Map<string, ((...args: any[]) => void)[]>();
    on = vi.fn((event: string, handler: (...args: any[]) => void) => {
      this.handlers.set(event, [...(this.handlers.get(event) || []), handler]); return this;
    });
    emit(event: string, ...args: any[]) { this.handlers.get(event)?.forEach(handler => handler(...args)); }
    off = vi.fn().mockReturnThis();
    startAudio = vi.fn(async () => undefined);
    connect = vi.fn(async () => { this.state = 'connected'; });
    disconnect = vi.fn(async () => { this.state = 'disconnected'; this.localParticipant.isMicrophoneEnabled = false; });
    getActiveDevice = vi.fn((kind: string) => kind === 'audioinput'
      ? this.options.audioCaptureDefaults?.deviceId || 'default' : 'default');
    constructor(options: MockRoom['options']) { this.options = options; MockRoom.instances.push(this); }
  }
  return { MockRoom };
});

vi.mock('livekit-client', () => ({
  Room: sdk.MockRoom,
  Track: { Source: { Camera: 'camera', Microphone:'microphone',ScreenShare:'screen',ScreenShareAudio:'screen_audio' } },
  AudioPresets:{musicHighQualityStereo:{}},
  VideoPresets: { h360: {}, h720: {} },
  VideoQuality: { LOW: 0, MEDIUM: 1, HIGH: 2 },
  RoomEvent: Object.fromEntries([
    'TrackSubscribed', 'TrackUnsubscribed', 'LocalTrackPublished', 'LocalTrackUnpublished',
    'ParticipantConnected', 'ParticipantDisconnected', 'ActiveSpeakersChanged', 'TrackMuted',
    'TrackUnmuted', 'Reconnecting', 'Reconnected', 'Disconnected', 'AudioPlaybackStatusChanged',
  ].map((name) => [name, name])),
  setLogLevel: vi.fn(),
}));

function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((done) => { resolve = done; });
  return { promise, resolve };
}

function createDevices() {
  const devices = {
    inputId: { value: 'usb-microphone' }, outputId: { value: 'headphones' }, supportsOutput: true,
    bindRoom: vi.fn((room: Parameters<Parameters<typeof useCall>[0]['bindRoom']>[0]) => {
      if (room) {
        devices.inputId.value = room.getActiveDevice('audioinput') || devices.inputId.value;
        devices.outputId.value = room.getActiveDevice('audiooutput') || devices.outputId.value;
      }
    }),
    selectOutput: vi.fn(async (_id: string) => true),
    refresh: vi.fn(async (_permission?: boolean) => undefined),
  };
  return devices;
}
const form = { name: 'Tester', password: 'local-test-password', room: 'test', withMic: false };
const invitation = { ok: true, json: async () => ({ url: 'ws://localhost:7880', token: 'test-token' }) };

describe('useCall lifecycle and audio devices', () => {
  const calls: ReturnType<typeof useCall>[] = [];
  function create(devices = createDevices()) {
    const call = useCall(devices, () => ({ videos: null, audio: null }));
    calls.push(call);
    return { call, devices };
  }
  beforeEach(() => {
    sdk.MockRoom.instances.length = 0;
    vi.stubGlobal('window', { isSecureContext: true, innerHeight: 1080, devicePixelRatio: 1 });
    vi.stubGlobal('navigator', { mediaDevices: { getDisplayMedia: vi.fn() } });
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(invitation));
  });
  afterEach(async () => {
    for (const call of calls.splice(0)) await call.leave();
    vi.unstubAllGlobals();
  });

  it('keeps a successful room connection when output switching returns false', async () => {
    const { call, devices } = create();
    devices.selectOutput.mockResolvedValue(false);
    await call.join(form);
    expect(call.connected.value).toBe(true);
    expect(call.status.value).toContain('Выход звука не переключился');
    expect(sdk.MockRoom.instances[0]!.disconnect).not.toHaveBeenCalled();
    expect(devices.selectOutput).toHaveBeenCalledWith('headphones');
  });

  it('aborts token loading and ignores a response that arrives after leave', async () => {
    const response = deferred<typeof invitation>();
    const fetchMock = vi.fn().mockReturnValue(response.promise);
    vi.stubGlobal('fetch', fetchMock);
    const { call } = create();
    const joining = call.join(form);
    const signal = fetchMock.mock.calls[0]![1].signal as AbortSignal;
    await call.leave();
    expect(signal.aborted).toBe(true);
    response.resolve(invitation);
    await joining;
    expect(sdk.MockRoom.instances).toHaveLength(0);
    expect(call.hasRoom.value).toBe(false);
    expect(call.joining.value).toBe(false);
    expect(call.status.value).toBe('Вы вышли из комнаты.');
  });

  it('refreshes devices after enabling the microphone following a muted join', async () => {
    const { call, devices } = create();
    await call.join(form);
    const room = sdk.MockRoom.instances[0]!;
    expect(room.localParticipant.setMicrophoneEnabled).not.toHaveBeenCalled();
    devices.refresh.mockClear();
    room.localParticipant.setMicrophoneEnabled.mockImplementation(async (enabled) => {
      room.localParticipant.isMicrophoneEnabled = enabled;
    });
    await call.toggle('mic');
    expect(devices.refresh).toHaveBeenCalledExactlyOnceWith();
    expect(call.mic.value).toBe(true);
  });

  it('preserves the selected microphone when joining muted and enabling it later', async () => {
    const { call } = create();
    await call.join(form);
    await call.toggle('mic');
    expect(sdk.MockRoom.instances[0]!.localParticipant.setMicrophoneEnabled)
      .toHaveBeenCalledWith(true, expect.objectContaining({ deviceId: {exact:'usb-microphone'} }));
  });

  it('stops a microphone capture that completes after leaving the room', async () => {
    const { call, devices } = create();
    await call.join(form);
    const room = sdk.MockRoom.instances[0]!;
    const publication = deferred<unknown>();
    const stop = vi.fn();
    room.localParticipant.setMicrophoneEnabled.mockReturnValue(publication.promise);
    devices.refresh.mockClear();
    const enabling = call.toggle('mic');
    await call.leave();
    publication.resolve({ track: { stop } });
    await enabling;
    expect(stop).toHaveBeenCalledOnce();
    expect(call.connected.value).toBe(false);
    expect(call.mic.value).toBe(false);
    expect(call.pending.value).toBe('');
    expect(devices.refresh).not.toHaveBeenCalled();
  });

  it('removes a muted camera tile and restores it once when the camera resumes', async () => {
    const figures: { remove: ReturnType<typeof vi.fn> }[] = [];
    vi.stubGlobal('document', { createElement: (tag: string) => {
      const element = { append: vi.fn(), remove: vi.fn(), className: '', textContent: '' };
      if (tag === 'figure') figures.push(element);
      return element;
    } });
    const element = { addEventListener:vi.fn(), removeEventListener:vi.fn(), play: vi.fn().mockResolvedValue(undefined), getBoundingClientRect: () => ({ width: 640, height: 360, top: 0, bottom: 360 }) };
    const track = { kind: 'video', isMuted: false, attach: vi.fn(() => element), detach: vi.fn() };
    const publication = { track, isMuted: false, kind: 'video', setEnabled: vi.fn(), setVideoDimensions: vi.fn() };
    const { call } = create(); await call.join(form);
    const room = sdk.MockRoom.instances[0]!;
    room.emit('TrackSubscribed', track, publication, { name: 'Remote' });
    expect(figures).toHaveLength(1);
    track.isMuted = publication.isMuted = true;
    room.emit('TrackMuted', publication, { name: 'Remote' });
    expect(figures[0]!.remove).toHaveBeenCalledOnce();
    expect(track.detach).toHaveBeenCalledWith(element);
    track.isMuted = publication.isMuted = false;
    room.emit('TrackUnmuted', publication, { name: 'Remote' });
    room.emit('TrackSubscribed', track, publication, { name: 'Remote' });
    expect(figures).toHaveLength(2);
    expect(track.attach).toHaveBeenCalledTimes(2);
    room.emit('TrackUnsubscribed', track);
    expect(figures[1]!.remove).toHaveBeenCalledOnce();
  });

  it('does not create a black tile for a subscription that starts muted', async () => {
    const { call } = create(); await call.join(form);
    const track = { kind: 'video', attach: vi.fn() };
    sdk.MockRoom.instances[0]!.emit('TrackSubscribed', track, { isMuted: true }, { name: 'Remote' });
    expect(track.attach).not.toHaveBeenCalled();
  });

  it('keeps camera and screen separate and enlarges the existing screen element without recapture', async () => {
    const figures:any[]=[];
    vi.stubGlobal('document',{createElement:(tag:string)=>{const node={append:vi.fn(),remove:vi.fn(),parentElement:null};if(tag==='figure')figures.push(node);return node;}});
    const makeTrack=(source:string)=>({kind:'video',source,isMuted:false,attach:vi.fn(()=>({addEventListener:vi.fn(),removeEventListener:vi.fn(),play:vi.fn().mockResolvedValue(undefined),getBoundingClientRect:()=>({width:640,height:360,top:0,bottom:360})})),detach:vi.fn(),stop:vi.fn()});
    const cameraTrack=makeTrack('camera'),screenTrack=makeTrack('screen_share');
    const {call}=create();await call.join(form);const room=sdk.MockRoom.instances[0]!;
    room.emit('LocalTrackPublished',{track:cameraTrack},room.localParticipant);
    room.emit('LocalTrackPublished',{track:screenTrack},room.localParticipant);
    expect((cameraTrack.attach.mock.results[0]!.value as unknown as HTMLElement).className).toContain('local-camera-preview');
    expect((screenTrack.attach.mock.results[0]!.value as unknown as HTMLElement).className||'').not.toContain('local-camera-preview');
    expect(call.videoTiles.value.map(t=>[t.participantId,t.source])).toEqual([['local','camera'],['local','screen']]);
    const screenTile=call.videoTiles.value.find(t=>t.source==='screen')!;
    function host(){return {append:vi.fn(function(this:any,node:any){node.parentElement=this;})} as unknown as HTMLElement;}
    const inline=host(),expanded=host();
    call.bindVideoHost(screenTile.id,inline);call.bindVideoHost(screenTile.id,expanded);call.bindVideoHost(screenTile.id,inline);
    expect(screenTrack.attach).toHaveBeenCalledOnce();expect(screenTrack.detach).not.toHaveBeenCalled();expect(screenTrack.stop).not.toHaveBeenCalled();
    expect(figures[1].parentElement).toBe(inline);expect(room.localParticipant.setScreenShareEnabled).not.toHaveBeenCalled();
    cameraTrack.isMuted=true;room.emit('TrackMuted',{track:cameraTrack});
    expect(call.videoTiles.value.map(t=>t.source)).toEqual(['screen']);expect(call.videoCount.value).toBe(1);
    room.emit('LocalTrackUnpublished',{track:screenTrack});expect(call.videoTiles.value).toEqual([]);
  });

  it.each(['NotAllowedError', 'NotReadableError', 'NotFoundError'])('explains camera failure %s without dropping the call', async name => {
    const { call } = create(); await call.join(form);
    sdk.MockRoom.instances[0]!.localParticipant.setCameraEnabled.mockRejectedValue(Object.assign(new Error('secret URL'), { name }));
    await call.toggle('camera');
    expect(call.status.value).toContain(name);
    expect(call.status.value).not.toContain('secret URL');
    expect(call.connected.value).toBe(true);
  });

  it('explains missing screen capture support without invoking the SDK or blocking other controls', async () => {
    vi.stubGlobal('navigator', { mediaDevices: {} });
    const { call } = create(); await call.join(form);
    await call.toggle('screen');
    expect(call.supportsScreen).toBe(false);
    expect(call.status.value).toContain('не поддерживает показ своего экрана');
    expect(sdk.MockRoom.instances[0]!.localParticipant.setScreenShareEnabled).not.toHaveBeenCalled();
    expect(call.pending.value).toBe('');
    await call.toggle('mic');
    expect(sdk.MockRoom.instances[0]!.localParticipant.setMicrophoneEnabled).toHaveBeenCalled();
    expect(call.connected.value).toBe(true);
  });

  it('starts supported screen capture immediately within the button gesture', async () => {
    const { call } = create(); await call.join(form);
    const capture = call.toggle('screen');
    expect(sdk.MockRoom.instances[0]!.localParticipant.setScreenShareEnabled).toHaveBeenCalledWith(true, expect.objectContaining({audio:expect.objectContaining({noiseSuppression:false})}),expect.objectContaining({dtx:false}));
    await capture;
  });

  it('restores local per-person gain across room identities without changing microphone volume', async()=>{
    const values=new Map<string,string>();vi.stubGlobal('localStorage',{getItem:(k:string)=>values.get(k)||null,setItem:(k:string,v:string)=>values.set(k,v)});
    const {call}=create();await call.join(form);const room=sdk.MockRoom.instances[0]!;
    const track={kind:'audio',attach:vi.fn(()=>({play:vi.fn().mockResolvedValue(undefined),remove:vi.fn()})),detach:vi.fn(),setVolume:vi.fn()};
    room.emit('TrackSubscribed',track,{}, {identity:'person.connection1',name:'Remote'});
    call.setParticipantVolume('person.connection1',160);expect(track.setVolume).toHaveBeenLastCalledWith(1.6);
    const second=create().call;expect(second.volumeFor('person.connection2')).toBe(160);
    expect(room.localParticipant.setMicrophoneEnabled).not.toHaveBeenCalled();
    call.setParticipantVolume('person.connection1',NaN);expect(second.volumeFor('person.connection2')).toBe(160);
  });

  it('updates screen proportions when metadata or resolution changes and removes listeners on unsubscribe', async () => {
    vi.stubGlobal('document',{createElement:()=>({append:vi.fn(),remove:vi.fn()})});
    const handlers=new Map<string,()=>void>();
    const video={videoWidth:0,videoHeight:0,play:vi.fn().mockResolvedValue(undefined),
      addEventListener:vi.fn((event:string,fn:()=>void)=>handlers.set(event,fn)),
      removeEventListener:vi.fn((event:string)=>handlers.delete(event)),
      getBoundingClientRect:()=>({width:960,height:270,top:0,bottom:270})};
    const track={kind:'video',source:'screen_share',isMuted:false,attach:vi.fn(()=>video),detach:vi.fn()};
    const {call}=create();await call.join(form);const room=sdk.MockRoom.instances[0]!;
    room.emit('TrackSubscribed',track,{isMuted:false},{identity:'remote',name:'Remote'});
    expect(call.videoTiles.value[0]?.aspectRatio).toBeUndefined();
    video.videoWidth=3840;video.videoHeight=1080;handlers.get('loadedmetadata')!();
    expect(call.videoTiles.value[0]?.aspectRatio).toBeCloseTo(32/9);
    const oldSnapshot=call.videoTiles.value[0];
    video.videoWidth=1080;video.videoHeight=1920;handlers.get('resize')!();
    expect(call.videoTiles.value[0]?.aspectRatio).toBeCloseTo(9/16);
    expect(oldSnapshot?.aspectRatio).toBeCloseTo(32/9);
    expect(track.attach).toHaveBeenCalledOnce();
    room.emit('TrackUnsubscribed',track);expect(handlers.size).toBe(0);
  });

  it('leaves incoming audio and microphone/output selection untouched when screen sharing starts', async () => {
    const audio={volume:0.8,muted:false,play:vi.fn().mockResolvedValue(undefined)};
    const track={kind:'audio',attach:vi.fn(()=>audio),detach:vi.fn()};
    const {call,devices}=create();await call.join(form);const room=sdk.MockRoom.instances[0]!;
    room.emit('TrackSubscribed',track,{}, {identity:'remote',name:'Remote'});
    devices.selectOutput.mockClear();devices.refresh.mockClear();
    await call.toggle('screen');
    expect(audio.volume).toBe(0.8);expect(audio.muted).toBe(false);
    expect(track.attach).toHaveBeenCalledOnce();expect(track.detach).not.toHaveBeenCalled();
    expect(room.localParticipant.setMicrophoneEnabled).not.toHaveBeenCalled();
    expect(devices.selectOutput).not.toHaveBeenCalled();expect(devices.refresh).not.toHaveBeenCalled();
    // Disconnect cleanup expects a media element, including remove().
    Object.assign(audio,{remove:vi.fn()});
  });

  it('explains runtime screen capture rejection without ending the call', async () => {
    const { call } = create(); await call.join(form);
    sdk.MockRoom.instances[0]!.localParticipant.setScreenShareEnabled.mockRejectedValue(Object.assign(new Error('secret URL'), { name: 'NotSupportedError' }));
    await call.toggle('screen');
    expect(call.status.value).toContain('не поддерживает показ своего экрана');
    expect(call.status.value).not.toContain('secret URL');
    expect(call.connected.value).toBe(true);
    expect(call.pending.value).toBe('');
  });

  it('keeps independent send/receive preferences without capturing a device', async () => {
    const { call } = create();
    expect(call.sendQuality.value).toBe('auto');
    expect(call.receiveQuality.value).toBe('auto');
    await call.selectSendQuality('720');
    call.selectReceiveQuality('audio');
    await call.join(form);
    expect(call.sendQuality.value).toBe('720');
    expect(call.receiveQuality.value).toBe('audio');
    expect(sdk.MockRoom.instances[0]!.localParticipant.setCameraEnabled).not.toHaveBeenCalled();
  });

  it('changes encoding without stopping, unpublishing or recapturing the camera', async () => {
    const { call } = create(); await call.join(form);
    const room = sdk.MockRoom.instances[0]!;
    room.localParticipant.isCameraEnabled = true;
    room.localParticipant.isMicrophoneEnabled = true;
    const setParameters = vi.fn(async (_value: any) => {});
    const track = { stop: vi.fn(), mediaStreamTrack: { getSettings: () => ({width:1920,height:1080}) },
      sender: {getParameters: () => ({encodings:[{maxBitrate:3_000_000}]}), setParameters} };
    room.localParticipant.videoTrackPublications.set('camera', {track});
    await call.selectSendQuality('360');
    expect(room.localParticipant.unpublishTrack).not.toHaveBeenCalled();
    expect(room.localParticipant.setCameraEnabled).not.toHaveBeenCalled();
    expect(track.stop).not.toHaveBeenCalled();
    expect(setParameters).toHaveBeenCalledWith({encodings:[{maxBitrate:600_000,scaleResolutionDownBy:3,maxFramerate:30}]});
    expect(room.disconnect).not.toHaveBeenCalled();
    expect(room.localParticipant.setMicrophoneEnabled).not.toHaveBeenCalled();
    expect(call.sendQuality.value).toBe('360');
    expect(call.mic.value).toBe(true);
  });

  it('applies one quality choice to both directions and rolls both back on encoding failure', async () => {
    const { call } = create(); await call.join(form);
    await call.selectQuality('720');
    expect(call.sendQuality.value).toBe('720'); expect(call.receiveQuality.value).toBe('720');
    const room = sdk.MockRoom.instances[0]!;
    room.localParticipant.videoTrackPublications.set('camera', {track:{
      mediaStreamTrack:{getSettings:()=>({width:1920,height:1080})},
      sender:{getParameters:()=>({encodings:[{maxBitrate:3_000_000}]}),setParameters:vi.fn().mockRejectedValue(new Error('unsupported'))}
    }});
    await call.selectQuality('360');
    expect(call.sendQuality.value).toBe('720'); expect(call.receiveQuality.value).toBe('720');
    expect(room.disconnect).not.toHaveBeenCalled();
  });
  it('does not turn on a muted camera when changing its quality', async () => {
    const { call } = create(); await call.join(form);
    const participant = sdk.MockRoom.instances[0]!.localParticipant;
    participant.getTrackPublication.mockImplementation((source?:unknown) => source === 'camera' ? { track: {} } : undefined);
    await call.selectSendQuality('1080');
    expect(participant.setCameraEnabled).not.toHaveBeenCalled();
    expect(call.camera.value).toBe(false);
    expect(call.sendQuality.value).toBe('1080');
  });

  it('does not recapture if the user leaves during a queued quality change', async () => {
    const { call } = create(); await call.join(form);
    const participant = sdk.MockRoom.instances[0]!.localParticipant;
    participant.isCameraEnabled = true;
    participant.getTrackPublication.mockImplementation((source?:unknown) => source === 'camera' ? { track: {} } : undefined);
    const changing = call.selectSendQuality('720');
    await call.leave(); await changing;
    expect(participant.setCameraEnabled).not.toHaveBeenCalled();
    expect(call.pending.value).toBe('');
  });

});
