import { AudioPresets, Track, type LocalParticipant } from 'livekit-client';

/** Check the actual capture before any audio reaches the room. */
export async function startScreenShare(participant: LocalParticipant, stillCurrent: () => boolean) {
  const tracks = await participant.createScreenTracks({
    selfBrowserSurface: 'exclude',
    surfaceSwitching: 'exclude',
    systemAudio: 'exclude',
    audio: {
      restrictOwnAudio: true,
      echoCancellation: false,
      noiseSuppression: false,
      autoGainControl: false,
    },
  });
  const video = tracks.find((track) => track.source === Track.Source.ScreenShare);
  const audio = tracks.find((track) => track.source === Track.Source.ScreenShareAudio);
  const surface = video?.mediaStreamTrack.getSettings().displaySurface;
  // A system mix can contain RTC playback even when restrictOwnAudio is reported true.
  // Chrome 107 introduced selfBrowserSurface. Older pickers can ignore the
  // exclusion and offer the tab playing the call as a capture source.
  const chromium = /(?:Chrome|Chromium)\/(\d+)/.exec(navigator.userAgent);
  const isolatedTab = surface === 'browser' && Number(chromium?.[1]) >= 107;
  const safeAudio = isolatedTab;
  const published = [];
  let keep = false;
  try {
    if (!video || !stillCurrent()) return;
    const publication = await participant.publishTrack(video);
    published.push(video);
    if (audio && safeAudio && stillCurrent()) {
      await participant.publishTrack(audio, {
        audioPreset: AudioPresets.musicHighQualityStereo,
        dtx: false,
      });
      published.push(audio);
    }
    keep = stillCurrent();
    if (keep) return publication;
  } finally {
    if (!keep)
      await Promise.allSettled(published.map((track) => participant.unpublishTrack(track)));
    for (const track of tracks) if (!keep || !published.includes(track)) track.stop();
  }
}
