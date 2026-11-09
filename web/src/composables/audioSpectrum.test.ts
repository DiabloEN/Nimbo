import { describe, it, expect } from 'vitest';
import { spectrumBars } from './audioSpectrum';
describe('measured spectrum', () => {
  it('keeps silence still and bounds loud input', () => {
    expect(
      spectrumBars(new Float32Array(1024).fill(-Infinity), 48000, 2048, Array(64).fill(7)),
    ).toEqual(Array(64).fill(7));
    const loud = spectrumBars(new Float32Array(1024).fill(0), 48000, 2048, Array(64).fill(78));
    expect(loud.every((x) => x >= 7 && x <= 78)).toBe(true);
  });
  it('responds differently to bass, mids and treble without random motion', () => {
    const outputs = [120, 1000, 8000].map((hz) => {
      const input = new Float32Array(1024).fill(-Infinity);
      input[Math.round((hz * 2048) / 48000)] = -20;
      const output = spectrumBars(input, 48000, 2048, Array(64).fill(7));
      expect(output).toEqual(spectrumBars(input, 48000, 2048, Array(64).fill(7)));
      expect(output.some((x) => x > 7)).toBe(true);
      expect(output.some((x) => x === 7)).toBe(true);
      return output;
    });
    expect(outputs[0]).not.toEqual(outputs[1]);
    expect(outputs[1]).not.toEqual(outputs[2]);
  });
});
