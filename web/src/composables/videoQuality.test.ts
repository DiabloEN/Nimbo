import { describe, expect, it, vi } from 'vitest';
import { VideoQuality, type RemoteTrackPublication } from 'livekit-client';
import { applyReceiveLimit, createVideoLimiter } from './videoQuality';

function videoSender() {
  let encodings: RTCRtpEncodingParameters[] = [
    { rid: 'q', active: false, scaleResolutionDownBy: 3, maxBitrate: 300_000 },
    { rid: 'h', active: true, scaleResolutionDownBy: 1.5, maxBitrate: 1_200_000 },
    { rid: 'f', active: true, scaleResolutionDownBy: 1, maxBitrate: 3_000_000 },
  ];
  const setParameters = vi.fn(async (parameters: RTCRtpSendParameters) => { encodings = parameters.encodings; });
  const sender = { getParameters: () => ({ encodings: encodings.map(e => ({ ...e })) }), setParameters } as unknown as RTCRtpSender;
  return { track: { sender, mediaStreamTrack: { getSettings: () => ({ width: 1920, height: 1080 }) } }, setParameters,
    encodings: () => encodings };
}

describe('outgoing video encoding limits', () => {
  it('caps the sum of simulcast bitrates, preserves active layers and restores resolution', async () => {
    const video = videoSender(), limiter = createVideoLimiter();
    await limiter.apply(video.track, '360');
    expect(video.encodings().reduce((sum, e) => sum + e.maxBitrate!, 0)).toBeLessThanOrEqual(600_000);
    expect(video.encodings().map(e => e.scaleResolutionDownBy)).toEqual([3, 3, 3]);
    expect(video.encodings().map(e => e.active)).toEqual([false, true, true]);
    await limiter.apply(video.track, 'auto');
    expect(video.encodings().map(e => e.scaleResolutionDownBy)).toEqual([3, 1.5, 1]);
    expect(video.encodings().reduce((sum, e) => sum + e.maxBitrate!, 0)).toBeLessThanOrEqual(3_000_000);
    expect(video.encodings()[2]!.maxBitrate).toBeGreaterThan(1_000_000);
    const count = video.setParameters.mock.calls.length;
    await limiter.apply(video.track, 'auto');
    expect(video.setParameters).toHaveBeenCalledTimes(count);
  });
  it('retries SDK parameter races and serializes successive choices', async () => {
    const video = videoSender(), limiter = createVideoLimiter();
    video.setParameters.mockRejectedValueOnce(new Error('transaction changed'));
    await Promise.all([limiter.apply(video.track, '360'), limiter.apply(video.track, '720')]);
    expect(video.setParameters).toHaveBeenCalledTimes(3);
    expect(video.encodings()[2]!.scaleResolutionDownBy).toBe(1.5);
  });
  it('ignores obsolete room work and missing senders without touching capture', async () => {
    const video = videoSender(), limiter = createVideoLimiter();
    await limiter.apply(video.track, '360', () => false);
    await limiter.apply({ mediaStreamTrack: video.track.mediaStreamTrack }, '360');
    expect(video.setParameters).not.toHaveBeenCalled();
  });
});

function remote(kind = 'video') {
  return { kind, dimensions: { width: 1920, height: 1080 }, setEnabled: vi.fn(), setVideoQuality: vi.fn(), setVideoDimensions: vi.fn() };
}
const display = { width: 640, height: 360, visible: true };
describe('incoming video limits', () => {
  it('requests lower SFU layers independently of outgoing preferences', () => {
    const publication = remote(), typed = publication as unknown as RemoteTrackPublication;
    applyReceiveLimit(typed, '360', display, false);
    expect(publication.setVideoQuality).toHaveBeenLastCalledWith(VideoQuality.LOW);
    applyReceiveLimit(typed, '720', display, false);
    expect(publication.setVideoQuality).toHaveBeenLastCalledWith(VideoQuality.MEDIUM);
    applyReceiveLimit(typed, '1080', display, false);
    expect(publication.setVideoQuality).toHaveBeenLastCalledWith(VideoQuality.HIGH);
  });
  it('pauses video in audio-only or hidden views, then resumes adaptive sizing', () => {
    const publication = remote(), typed = publication as unknown as RemoteTrackPublication;
    applyReceiveLimit(typed, 'audio', display, false);
    expect(publication.setEnabled).toHaveBeenLastCalledWith(false);
    expect(publication.setVideoQuality).not.toHaveBeenCalled();
    applyReceiveLimit(typed, 'auto', display, true);
    expect(publication.setEnabled).toHaveBeenLastCalledWith(false);
    applyReceiveLimit(typed, 'auto', display, false);
    expect(publication.setEnabled).toHaveBeenLastCalledWith(true);
    expect(publication.setVideoDimensions).toHaveBeenLastCalledWith({ width: 640, height: 360 });
  });
  it('never disables audio and respects portrait dimensions', () => {
    const audio = remote('audio');
    applyReceiveLimit(audio as unknown as RemoteTrackPublication, 'audio', display, false);
    expect(audio.setEnabled).not.toHaveBeenCalled();
    const publication = remote(); publication.dimensions = { width: 1080, height: 1920 };
    applyReceiveLimit(publication as unknown as RemoteTrackPublication, 'auto', { width: 540, height: 960, visible: true }, false);
    expect(publication.setVideoDimensions).toHaveBeenCalledWith({ width: 540, height: 960 });
  });
});
