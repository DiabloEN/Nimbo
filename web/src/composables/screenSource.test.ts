import { describe, expect, it } from 'vitest';
import { screenSurface, screenTrackName, screenSourceLabel } from './screenSource';
describe('screen source labels', () => {
  it.each([
    ['browser', 'вкладка'],
    ['window', 'окно'],
    ['monitor', 'экран'],
  ])('preserves %s across publication', (source, label) => {
    expect(screenSourceLabel(screenSurface(screenTrackName(source)))).toBe(label);
  });
  it('does not guess the capture source for older clients or arbitrary track names', () => {
    for (const name of [
      undefined,
      'screen',
      'screen_share',
      'private document title',
      'nimbo-screen:unknown',
    ]) {
      expect(screenSurface(name)).toBeUndefined();
      expect(screenSourceLabel(screenSurface(name))).toBe('трансляция');
    }
    expect(screenTrackName('private document title')).toBe('nimbo-screen');
  });
});
