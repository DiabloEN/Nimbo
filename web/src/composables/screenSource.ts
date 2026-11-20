const surfaces = ['browser', 'window', 'monitor'] as const;
export function screenTrackName(surface?: string) {
  return surfaces.some((value) => value === surface) ? `nimbo-screen:${surface}` : 'nimbo-screen';
}
export function screenSurface(name?: string) {
  return surfaces.find((value) => name === screenTrackName(value));
}
export function screenSourceLabel(surface?: string) {
  return surface === 'browser'
    ? 'вкладка'
    : surface === 'window'
      ? 'окно'
      : surface === 'monitor'
        ? 'экран'
        : 'трансляция';
}
