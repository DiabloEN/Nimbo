import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { Room } from 'livekit-client';
import { useAudioDevices } from './useAudioDevices';
import { audioLabEnabled } from './audioLab';

function device(kind: MediaDeviceKind, deviceId: string, label: string): MediaDeviceInfo {
  return { kind, deviceId, label, groupId: '', toJSON() { return {}; } };
}
function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((done) => { resolve = done; });
  return { promise, resolve };
}

describe('useAudioDevices', () => {
  let media: {
    enumerateDevices: ReturnType<typeof vi.fn>;
    getUserMedia: ReturnType<typeof vi.fn>;
    addEventListener: ReturnType<typeof vi.fn>;
    removeEventListener: ReturnType<typeof vi.fn>;
  };
  const instances: ReturnType<typeof useAudioDevices>[] = [];
  function create() { const state = useAudioDevices(); instances.push(state); return state; }
  beforeEach(() => {
    media = {
      enumerateDevices: vi.fn().mockResolvedValue([]), getUserMedia: vi.fn(),
      addEventListener: vi.fn(), removeEventListener: vi.fn(),
    };
    vi.stubGlobal('navigator', { mediaDevices: media });
    vi.stubGlobal('HTMLMediaElement', class { setSinkId() {} });
  });
  afterEach(() => {
    instances.splice(0).forEach((instance) => instance.dispose());
    vi.unstubAllGlobals();
  });

  it('explains hidden devices without implicitly requesting microphone access', async () => {
    const state = create();
    await state.refresh();
    expect(media.getUserMedia).not.toHaveBeenCalled();
    expect(state.inputs.value).toHaveLength(1);
    expect(state.note.value).toContain('Обновить список устройств');
    expect(state.busy.value).toBe(false);
  });

  it('names the default input and output using the browser aliases', async () => {
    media.enumerateDevices.mockResolvedValue([
      device('audioinput', 'default', 'По умолчанию - USB Mic'),
      device('audiooutput', 'default', 'Default - Headphones'),
    ]);
    const state = create(); await state.refresh();
    expect(state.inputs.value[0]!.label).toBe('По умолчанию (USB Mic)');
    expect(state.outputs.value[0]!.label).toBe('По умолчанию (Headphones)');
    expect(state.inputId.value).toBe('default');
    media.enumerateDevices.mockResolvedValue([device('audioinput', 'default', 'Default - New Mic')]);
    await state.refresh();
    expect(state.inputs.value[0]!.label).toBe('По умолчанию (New Mic)');
  });

  it('uses a matching physical device group but never guesses the default from list order', async () => {
    media.enumerateDevices.mockResolvedValue([
      { ...device('audioinput', 'default', 'Default'), groupId: 'usb' },
      { ...device('audioinput', 'physical', 'USB Mic'), groupId: 'usb' },
      device('audiooutput', 'first', 'Speakers'),
    ]);
    const state = create(); await state.refresh();
    expect(state.inputs.value[0]!.label).toBe('По умолчанию (USB Mic)');
    expect(state.outputs.value[0]!.label).toBe('По умолчанию (название недоступно)');
  });

  it('enumerates outputs even after microphone permission is denied', async () => {
    media.getUserMedia.mockRejectedValue(Object.assign(new Error(), { name: 'NotAllowedError' }));
    media.enumerateDevices.mockResolvedValue([device('audiooutput', 'speakers', 'Speakers')]);
    const state = create();
    await state.refresh(true);
    expect(media.enumerateDevices).toHaveBeenCalledOnce();
    expect(state.outputs.value.some((item) => item.deviceId === 'speakers')).toBe(true);
    expect(state.note.value).toContain('запрещён');
  });

  it('stops all temporary tracks even when enumeration fails', async () => {
    const stop = vi.fn();
    media.getUserMedia.mockResolvedValue({ getTracks: () => [{ stop }, { stop }] });
    media.enumerateDevices.mockRejectedValue(new Error('enumeration failed'));
    const state = create();
    await state.refresh(true);
    expect(stop).toHaveBeenCalledTimes(2);
    expect(state.note.value).toContain('список устройств');
    expect(state.busy.value).toBe(false);
  });

  it('ignores an earlier enumeration that finishes after a newer refresh', async () => {
    const first = deferred<MediaDeviceInfo[]>();
    media.enumerateDevices.mockReturnValueOnce(first.promise)
      .mockResolvedValueOnce([device('audioinput', 'new', 'New microphone')]);
    const state = create();
    const earlier = state.refresh();
    await state.refresh();
    first.resolve([device('audioinput', 'old', 'Old microphone')]);
    await earlier;
    expect(state.inputs.value.map((item) => item.deviceId)).toEqual(['default', 'new']);
  });

  it.each([false, new Error('device rejected')])('keeps the prior selection after a failed switch (%s)', async (result) => {
    const state = create();
    await state.selectInput('old');
    const switchActiveDevice = result instanceof Error
      ? vi.fn().mockRejectedValue(result) : vi.fn().mockResolvedValue(result);
    const disconnect = vi.fn();
    const room = { on: vi.fn(), off: vi.fn(), getActiveDevice: vi.fn(), switchActiveDevice, disconnect };
    state.bindRoom(room as unknown as Room);
    expect(await state.selectInput('new')).toBe(false);
    expect(state.inputId.value).toBe('old');
    expect(state.note.value).toContain('Проверьте активный микрофон');
    expect(disconnect).not.toHaveBeenCalled();
  });

  it('keeps output failures recoverable and never disconnects the room', async () => {
    const state = create();
    const room = {
      on: vi.fn(), off: vi.fn(), getActiveDevice: vi.fn(),
      switchActiveDevice: vi.fn().mockRejectedValue(new Error('denied')), disconnect: vi.fn(),
    };
    state.bindRoom(room as unknown as Room);
    expect(await state.selectOutput('headphones')).toBe(false);
    expect(state.outputId.value).toBe('default');
    expect(state.note.value).toContain('Звонок продолжается');
    expect(room.disconnect).not.toHaveBeenCalled();
  });

  it('reconciles an unplugged selected device with the actual room', async () => {
    const state = create();
    await state.selectInput('unplugged');
    let actual = 'unplugged';
    const room = {
      on: vi.fn(), off: vi.fn(),
      getActiveDevice: vi.fn((kind: string) => kind === 'audioinput' ? actual : undefined),
      switchActiveDevice: vi.fn(async (_kind: string, id: string) => { actual = id; return true; }),
    };
    state.bindRoom(room as unknown as Room);
    media.enumerateDevices.mockResolvedValue([device('audioinput', 'built-in', 'Built-in microphone')]);
    await state.refresh();
    expect(room.switchActiveDevice).toHaveBeenCalledWith('audioinput', 'default');
    expect(state.inputId.value).toBe('default');
  });


  it('serializes microphone publication behind a hardware switch', async () => {
    const state=create(),wait=deferred<boolean>();let actual='old';
    const room={on:vi.fn(),off:vi.fn(),getActiveDevice:vi.fn(()=>actual),switchActiveDevice:vi.fn(async()=>{await wait.promise;actual='new';return true;})};
    state.bindRoom(room as unknown as Room);
    const switching=state.selectInput('new');const enable=vi.fn(async()=>state.inputId.value);
    const enabling=state.runInput(enable);await Promise.resolve();expect(enable).not.toHaveBeenCalled();
    wait.resolve(true);await switching;expect(await enabling).toBe('new');expect(state.busy.value).toBe(false);
  });
  it('ignores a late switch result from the room already left', async () => {
    const state=create(),wait=deferred<boolean>();state.inputId.value='old';
    const room={on:vi.fn(),off:vi.fn(),getActiveDevice:vi.fn(()=> 'new'),switchActiveDevice:vi.fn(()=>wait.promise)};
    state.bindRoom(room as unknown as Room);const switching=state.selectInput('new');await Promise.resolve();state.bindRoom(null);wait.resolve(true);
    expect(await switching).toBe(false);expect(state.inputId.value).toBe('old');
  });
  it('rejects an acknowledged switch when the captured microphone is still different',async()=>{
    const state=create();state.inputId.value='old';
    const track={isMuted:false,getDeviceId:vi.fn(async()=> 'old')};
    const room={on:vi.fn(),off:vi.fn(),localParticipant:{getTrackPublication:()=>({track})},switchActiveDevice:vi.fn(async()=>true)};
    state.bindRoom(room as unknown as Room);
    expect(await state.selectInput('new')).toBe(false);
    expect(state.inputId.value).toBe('old');
    expect(room.switchActiveDevice).toHaveBeenLastCalledWith('audioinput','old');
  });
  it('accepts the default alias resolving to a concrete microphone id',async()=>{
    const state=create();state.inputId.value='old';
    state.lab.engine.value='browser';
    const track={isMuted:false,constraints:{noiseSuppression:true,autoGainControl:false},getProcessor:()=>undefined,getDeviceId:vi.fn(async()=> 'physical-default-id')};
    const room={on:vi.fn(),off:vi.fn(),localParticipant:{getTrackPublication:()=>({track})},switchActiveDevice:vi.fn(async()=>false)};
    state.bindRoom(room as unknown as Room);
    expect(await state.selectInput('default')).toBe(true);
    expect(state.inputId.value).toBe('default');
  });
  it.runIf(audioLabEnabled)('uses GTCRN by default on the microphone and releases it when switching to off', async () => {
    vi.stubGlobal('localStorage', {getItem: () => null, setItem: vi.fn()});
    const state = create();
    let processor: {name: string} | undefined;
    const track = {
      isMuted: false, constraints: state.captureOptions(), getProcessor: () => processor,
      setProcessor: vi.fn(async (next: {name: string}) => { processor = next; }),
      stopProcessor: vi.fn(async () => { processor = undefined; }),
    };
    vi.stubGlobal('AudioWorkletNode', class {});
    state.bindRoom({on: vi.fn(), off: vi.fn(), localParticipant: {getTrackPublication: () => ({track})}} as unknown as Room);
    await state.updateProcessing();
    expect(processor?.name).toBe('nimbo-gtcrn');
    expect(track.constraints.noiseSuppression).toBe(false);
    state.lab.suppression.value = false;
    await state.updateProcessing();
    expect(track.stopProcessor).toHaveBeenCalledOnce();
    expect(processor).toBeUndefined();
  });
  it.runIf(audioLabEnabled)('restores a saved mode, including disabled suppression, on reopening', () => {
    const storage = new Map<string, string>();
    vi.stubGlobal('localStorage', {getItem: (key: string) => storage.get(key) ?? null,
      setItem: (key: string, value: string) => storage.set(key, value)});
    const first = create().lab;
    first.engine.value = 'rnnoise';
    first.save();
    expect(create().lab.engine.value).toBe('rnnoise');
    first.suppression.value = false;
    first.save();
    expect(create().lab.suppression.value).toBe(false);
  });
  it.each([true, false])('ignores legacy gain and falls back from a removed engine (suppression=%s)', suppression => {
    const saved = {engine: 'speex', gain: true, suppression};
    const setItem = vi.fn();
    vi.stubGlobal('localStorage', {getItem: () => JSON.stringify(saved), setItem});
    const lab = create().lab;
    expect(lab.captureOptions().autoGainControl).toBe(false);
    if (audioLabEnabled) {
      expect(lab.engine.value).toBe('gtcrn');
      expect(lab.suppression.value).toBe(suppression);
      lab.save();
      expect(JSON.parse(setItem.mock.calls[0]![1])).toEqual({engine: 'gtcrn', suppression});
    }
  });
  it('stops a late permission probe after disposal without updating state', async () => {
    const permission = deferred<MediaStream>();
    const stop = vi.fn();
    media.getUserMedia.mockReturnValue(permission.promise);
    const state = create();
    const pending = state.refresh(true);
    state.dispose();
    permission.resolve({ getTracks: () => [{ stop }] } as unknown as MediaStream);
    await pending;
    expect(stop).toHaveBeenCalledOnce();
    expect(media.enumerateDevices).not.toHaveBeenCalled();
    expect(state.busy.value).toBe(false);
  });

  it('does not replace user preference with SDK device events and removes listeners when disposed', () => {
    const state = create();
    const room = { on: vi.fn(), off: vi.fn(), getActiveDevice: vi.fn() };
    state.bindRoom(room as unknown as Room);
    const listener = room.on.mock.calls[0]![1] as (kind: string, id: string) => void;
    listener('audioinput', 'usb');
    expect(state.inputId.value).toBe('default');
    state.dispose();
    expect(room.off).toHaveBeenCalledWith('activeDeviceChanged', listener);
    expect(media.removeEventListener).toHaveBeenCalledWith('devicechange', expect.any(Function));
  });
});
