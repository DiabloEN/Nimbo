import { LocalAudioTrack, LocalVideoTrack, Track, type LocalTrack } from 'livekit-client';

/** Call getDisplayMedia directly: LiveKit 2.23 drops windowAudio from its options. */
export async function captureScreen(): Promise<LocalTrack[]> {
  const safari =
    /Safari\//.test(navigator.userAgent) && !/(?:Chrome|Chromium|Edg)\//.test(navigator.userAgent);
  const options: DisplayMediaStreamOptions & {
    audio: MediaTrackConstraints & { restrictOwnAudio: boolean };
    windowAudio: 'window';
    systemAudio: 'exclude';
    selfBrowserSurface: 'exclude';
    surfaceSwitching: 'exclude';
  } = {
    video: safari ? true : { width: { ideal: 1920 }, height: { ideal: 1080 }, frameRate: 30 },
    audio: {
      restrictOwnAudio: true,
      echoCancellation: false,
      noiseSuppression: false,
      autoGainControl: false,
    },
    // Request the selected application's audio, never the whole system mix.
    windowAudio: 'window',
    systemAudio: 'exclude',
    selfBrowserSurface: 'exclude',
    surfaceSwitching: 'exclude',
  };
  const stream = await navigator.mediaDevices.getDisplayMedia(options);
  const tracks: LocalTrack[] = [];
  try {
    const rawVideo = stream.getVideoTracks()[0];
    if (!rawVideo) throw new Error('Browser returned no screen video');
    const video = new LocalVideoTrack(rawVideo, undefined, false);
    video.source = Track.Source.ScreenShare;
    tracks.push(video);
    const rawAudio = stream.getAudioTracks()[0];
    if (rawAudio) {
      const audio = new LocalAudioTrack(rawAudio, undefined, false);
      audio.source = Track.Source.ScreenShareAudio;
      tracks.push(audio);
    }
    for (const raw of stream.getTracks()) {
      if (raw !== rawVideo && raw !== rawAudio) raw.stop();
    }
    return tracks;
  } catch (error) {
    for (const track of tracks) track.stop();
    for (const track of stream.getTracks()) track.stop();
    throw error;
  }
}
