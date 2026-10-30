import { VideoQuality, type RemoteTrackPublication } from 'livekit-client';

export type SendQuality = 'auto' | '360' | '720' | '1080';
export type ReceiveQuality = SendQuality | 'audio';
export const videoModes = {
  auto: { height: 1080, bitrate: 3_000_000 },
  '360': { height: 360, bitrate: 600_000 },
  '720': { height: 720, bitrate: 1_700_000 },
  '1080': { height: 1080, bitrate: 3_000_000 },
};
type VideoSender = { sender?: RTCRtpSender; mediaStreamTrack: Pick<MediaStreamTrack, 'getSettings'> };

// Keep the capture and publication alive. Only RTP encoding parameters change.
// Always use fresh parameters; LiveKit also updates active simulcast layers.
export function createVideoLimiter() {
  const originals = new WeakMap<RTCRtpSender, RTCRtpEncodingParameters[]>();
  const queues = new WeakMap<RTCRtpSender, Promise<void>>();
  function apply(track: VideoSender, quality: SendQuality, valid: () => boolean = () => true) {
    const sender = track.sender;
    if (!sender) return Promise.resolve();
    const task = (queues.get(sender) ?? Promise.resolve()).catch(() => {}).then(async () => {
      for (let attempt = 0; attempt < 2; attempt++) {
        if (!valid() || track.sender !== sender) return;
        const parameters = sender.getParameters();
        if (!parameters.encodings?.length) return;
        let baseline = originals.get(sender);
        if (!baseline || baseline.length !== parameters.encodings.length) {
          baseline = parameters.encodings.map(encoding => ({ ...encoding }));
          originals.set(sender, baseline);
        }
        const mode = videoModes[quality];
        const settings = track.mediaStreamTrack.getSettings();
        const shortSide = Math.min(settings.width || 1920, settings.height || 1080);
        const total = baseline.reduce((sum, item) => sum + (item.maxBitrate || mode.bitrate / baseline!.length), 0);
        const factor = Math.min(1, mode.bitrate / total);
        let changed = false;
        parameters.encodings.forEach((encoding, index) => {
          const original = baseline![index]!;
          const scale = Math.max(original.scaleResolutionDownBy || 1, shortSide / mode.height, 1);
          const bitrate = Math.floor((original.maxBitrate || mode.bitrate / baseline!.length) * factor);
          const fps = Math.min(original.maxFramerate || 30, 30);
          changed ||= encoding.scaleResolutionDownBy !== scale || encoding.maxBitrate !== bitrate || encoding.maxFramerate !== fps;
          encoding.scaleResolutionDownBy = scale;
          encoding.maxBitrate = bitrate;
          encoding.maxFramerate = fps;
        });
        if (!changed) return;
        try { await sender.setParameters(parameters); return; }
        catch (error) { if (attempt === 1) throw error; }
      }
    });
    queues.set(sender, task);
    return task;
  }
  return { apply };
}

// SFU subscription requests cap each received video. Congestion control can
// reduce it further. This is not a hard aggregate Mbps cap for the whole room.
export function applyReceiveLimit(publication: RemoteTrackPublication, quality: ReceiveQuality,
  display: { width: number; height: number; visible: boolean }, hidden: boolean) {
  if (publication.kind !== 'video') return;
  const enabled = quality !== 'audio' && !hidden && display.visible;
  publication.setEnabled(enabled);
  if (!enabled) return;
  if (quality !== 'auto') {
    const sourceHeight = publication.dimensions ? Math.min(publication.dimensions.width, publication.dimensions.height) : 1080;
    // Nimbo publishes 360/720/1080 simulcast layers. Smaller native sources
    // can use their top layer. Other publishers may offer fewer/lower layers.
    publication.setVideoQuality(sourceHeight <= videoModes[quality as SendQuality].height ? VideoQuality.HIGH
      : quality === '360' ? VideoQuality.LOW : VideoQuality.MEDIUM);
    return;
  }
  const max = videoModes[quality as SendQuality].height;
  const source = publication.dimensions;
  const ratio = source?.width && source.height ? source.width / source.height : 16 / 9;
  const cap = ratio >= 1 ? { width: max * ratio, height: max } : { width: max, height: max / ratio };
  const fit = Math.min(1, Math.max(display.width / cap.width, display.height / cap.height));
  publication.setVideoDimensions({ width: Math.max(1, Math.round(cap.width * fit)), height: Math.max(1, Math.round(cap.height * fit)) });
}
