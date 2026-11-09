// Observe the existing microphone track. Never capture another mic or play it.
export function createSpeakingMeter(
  track: () => MediaStreamTrack | undefined,
  change: (speaking: boolean | undefined) => void,
) {
  let context: AudioContext | undefined,
    source: MediaStreamAudioSourceNode | undefined,
    analyser: AnalyserNode | undefined;
  let current: MediaStreamTrack | undefined, samples: Float32Array<ArrayBuffer> | undefined;
  let timer: ReturnType<typeof setInterval> | undefined,
    lastVoice = 0,
    state: boolean | undefined;
  function emit(value: boolean | undefined) {
    if (state !== value) {
      state = value;
      change(value);
    }
  }
  function release() {
    source?.disconnect();
    source = undefined;
    analyser?.disconnect();
    analyser = undefined;
    void context?.close().catch(() => {});
    context = undefined;
    samples = undefined;
    current = undefined;
    lastVoice = 0;
  }
  function tick() {
    const next = track();
    if (!next || next.readyState === 'ended' || !next.enabled) {
      if (current) release();
      emit(false);
      return;
    }
    if (next !== current) {
      release();
      current = next;
      try {
        context = new AudioContext();
        source = context.createMediaStreamSource(new MediaStream([next]));
        analyser = context.createAnalyser();
        analyser.fftSize = 512;
        analyser.smoothingTimeConstant = 0;
        source.connect(analyser);
        samples = new Float32Array(512);
        void context.resume().catch(() => {});
      } catch {
        emit(undefined);
        return;
      }
    }
    if (!analyser || !samples || context?.state !== 'running') {
      emit(undefined);
      return;
    }
    analyser.getFloatTimeDomainData(samples);
    const rms = Math.sqrt(samples.reduce((sum, value) => sum + value * value, 0) / samples.length),
      now = performance.now();
    if (rms > (state ? 0.007 : 0.012)) lastVoice = now;
    emit(lastVoice > 0 && now - lastVoice < 160);
  }
  function stop() {
    clearInterval(timer);
    timer = undefined;
    release();
    emit(undefined);
  }
  function start() {
    stop();
    tick();
    timer = setInterval(tick, 50);
  }
  return { start, stop };
}
