import { afterEach, expect, it, vi } from 'vitest';
import { captureScreen } from './captureScreen';
import { startScreenShare } from './shareScreen';

vi.mock('livekit-client', () => {
  class LocalTrack {
    source = '';
    constructor(public mediaStreamTrack: MediaStreamTrack) {}
    stop() {
      this.mediaStreamTrack.stop();
    }
  }
  return {
    LocalVideoTrack: LocalTrack,
    LocalAudioTrack: LocalTrack,
    Track: { Source: { ScreenShare: 'screen', ScreenShareAudio: 'screen_audio' } },
    AudioPresets: { musicHighQualityStereo: {} },
  };
});
afterEach(() => vi.unstubAllGlobals());

function setup(surface = 'window') {
  const video = { getSettings: () => ({ displaySurface: surface }), stop: vi.fn() };
  const audio = { getSettings: () => ({}), stop: vi.fn() };
  const stream = {
    getVideoTracks: () => [video],
    getAudioTracks: () => [audio],
    getTracks: () => [video, audio],
  };
  const capture = vi.fn(async () => stream);
  vi.stubGlobal('navigator', {
    userAgent: 'Chrome/158.0',
    mediaDevices: { getDisplayMedia: capture },
  });
  const participant = {
    publishTrack: vi.fn(async (track: any) => ({ track })),
    unpublishTrack: vi.fn(async () => undefined),
  };
  return { video, audio, stream, capture, participant };
}

it('passes window audio and call exclusion to the native picker in the user gesture', async () => {
  const { capture } = setup();
  const pending = captureScreen();
  expect(capture).toHaveBeenCalledWith(
    expect.objectContaining({
      windowAudio: 'window',
      systemAudio: 'exclude',
      selfBrowserSurface: 'exclude',
      audio: expect.objectContaining({ restrictOwnAudio: true, noiseSuppression: false }),
    }),
  );
  const tracks = await pending;
  expect(tracks.map((t) => t.source)).toEqual(['screen', 'screen_audio']);
});

it.each(['window', 'browser'])(
  'publishes the actual %s audio and retains the capture',
  async (surface) => {
    const { participant, audio, video } = setup(surface);
    await startScreenShare(participant as any, () => true);
    expect(participant.publishTrack.mock.calls.map(([t]) => t.source)).toEqual([
      'screen',
      'screen_audio',
    ]);
    expect(audio.stop).not.toHaveBeenCalled();
    expect(video.stop).not.toHaveBeenCalled();
    expect(participant.publishTrack).toHaveBeenCalledWith(
      expect.objectContaining({ source: 'screen' }),
      expect.objectContaining({ screenShareEncoding: { maxBitrate: 5_000_000, maxFramerate: 30 } }),
    );
  },
);

it('never publishes a whole-monitor audio mix as isolated window audio', async () => {
  const { participant, audio } = setup('monitor');
  await startScreenShare(participant as any, () => true);
  expect(participant.publishTrack).toHaveBeenCalledTimes(1);
  expect(audio.stop).toHaveBeenCalledOnce();
});

it('stops all capture tracks when the call ends while the picker is open', async () => {
  const { participant, video, audio } = setup();
  await startScreenShare(participant as any, () => false);
  expect(participant.publishTrack).not.toHaveBeenCalled();
  expect(video.stop).toHaveBeenCalledOnce();
  expect(audio.stop).toHaveBeenCalledOnce();
});

it('rolls back video and stops both tracks if audio publication fails', async () => {
  const { participant, video, audio } = setup();
  participant.publishTrack
    .mockImplementationOnce(async (t) => ({ track: t }))
    .mockRejectedValueOnce(new Error('publish failed'));
  await expect(startScreenShare(participant as any, () => true)).rejects.toThrow('publish failed');
  expect(participant.unpublishTrack).toHaveBeenCalledOnce();
  expect(video.stop).toHaveBeenCalledOnce();
  expect(audio.stop).toHaveBeenCalledOnce();
});

it('stops audio if the browser returns a stream without video', async () => {
  const { stream, audio } = setup();
  stream.getVideoTracks = () => [];
  await expect(captureScreen()).rejects.toThrow('no screen video');
  expect(audio.stop).toHaveBeenCalledOnce();
});

it('keeps video working when the user has not shared audio', async () => {
  const { stream, participant, video } = setup();
  stream.getAudioTracks = () => [];
  await startScreenShare(participant as any, () => true);
  expect(participant.publishTrack).toHaveBeenCalledOnce();
  expect(video.stop).not.toHaveBeenCalled();
});
