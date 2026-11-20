import { AudioPresets, Track, type LocalParticipant } from 'livekit-client';
import { screenTrackName } from './screenSource';
import { captureScreen } from './captureScreen';

/** Check the actual capture before any audio reaches the room. */
export async function startScreenShare(participant: LocalParticipant, stillCurrent: () => boolean) {
  const tracks = await captureScreen();
  const video = tracks.find((track) => track.source === Track.Source.ScreenShare);
  const audio = tracks.find((track) => track.source === Track.Source.ScreenShareAudio);
  const surface = video?.mediaStreamTrack.getSettings().displaySurface;
  // The picker requests per-application audio for windows and excludes this tab.
  // A monitor mix can include the call, so do not publish it as application audio.
  const safeAudio = surface === 'browser' || surface === 'window';
  const published = [];
  let keep = false;
  try {
    if (!video || !stillCurrent()) return;
    video.mediaStreamTrack.contentHint = 'motion';
    const publication = await participant.publishTrack(video, {
      name: screenTrackName(surface),
      // The SDK defaults to 1080p/15, which is unsuitable for moving games.
      screenShareEncoding: { maxBitrate: 5_000_000, maxFramerate: 30 },
    });
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
