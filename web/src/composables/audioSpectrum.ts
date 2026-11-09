// Fixed interleaving spreads bass, mids and treble across the strip. Each bar
// always represents the same measured band; silence never generates motion.
export function spectrumBars(
  db: Float32Array,
  sampleRate: number,
  fftSize: number,
  previous: number[],
): number[] {
  const count = 64,
    low = 70,
    high = Math.min(14000, sampleRate / 2);
  return Array.from({ length: count }, (_, i) => {
    const band = (i * 23) % count;
    const start = low * (high / low) ** (band / count),
      end = low * (high / low) ** ((band + 1) / count);
    const from = Math.max(1, Math.floor((start * fftSize) / sampleRate));
    const to = Math.min(db.length - 1, Math.max(from, Math.ceil((end * fftSize) / sampleRate)));
    let peak = -Infinity;
    for (let bin = from; bin <= to; bin++) peak = Math.max(peak, db[bin] ?? -Infinity);
    const weighted = peak + Math.max(0, 3 * Math.log2(start / 200));
    const energy = Math.max(0, Math.min(1, (weighted + 75) / 65));
    const target = 7 + energy ** 0.8 * 71,
      old = previous[i] ?? 7;
    return Math.round((old + (target - old) * (target > old ? 0.55 : 0.22)) * 10) / 10;
  });
}
