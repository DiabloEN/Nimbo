import { beforeEach, afterEach, it, expect, vi } from 'vitest';
import { ref, nextTick } from 'vue';
import { useHotkeys } from './useHotkeys';
const lifecycle = vi.hoisted(() => ({ cleanup: [] as (() => void)[] }));
vi.mock('vue', async () => ({
  ...(await vi.importActual<any>('vue')),
  onMounted: (fn: () => void) => fn(),
  onBeforeUnmount: (fn: () => void) => lifecycle.cleanup.push(fn),
}));
let win: EventTarget;
let data: Map<string, string>;
function event(type: string, extra: Record<string, unknown> = {}) {
  return Object.assign(new Event(type, { cancelable: true }), {
    code: 'KeyM',
    button: 3,
    ctrlKey: false,
    altKey: false,
    shiftKey: false,
    metaKey: false,
    repeat: false,
    isComposing: false,
    getModifierState: () => false,
    ...extra,
  });
}
beforeEach(() => {
  win = new EventTarget();
  data = new Map();
  vi.stubGlobal('window', win);
  vi.stubGlobal('document', { activeElement: null });
  vi.stubGlobal('localStorage', {
    getItem: (k: string) => data.get(k) || null,
    setItem: (k: string, v: string) => data.set(k, v),
  });
});
afterEach(() => {
  lifecycle.cleanup.splice(0).forEach((fn) => fn());
  vi.unstubAllGlobals();
});
it('fires a side-button binding even when the browser cancels its native navigation, only once per click', () => {
  const action = vi.fn(),
    keys = useHotkeys(ref('test'), action);
  keys.set('camera', { code: 'Mouse4', ctrl: false, alt: false, shift: false, meta: false });
  const down = event('mousedown');
  down.preventDefault();
  win.dispatchEvent(down);
  const up = event('mouseup'),
    aux = event('auxclick');
  win.dispatchEvent(up);
  win.dispatchEvent(aux);
  expect(action).toHaveBeenCalledExactlyOnceWith('camera');
  expect(up.defaultPrevented).toBe(true);
  expect(aux.defaultPrevented).toBe(true);
});
it('does not trigger while typing, recording, composing or holding a key', () => {
  const action = vi.fn(),
    keys = useHotkeys(ref('test'), action);
  keys.set('mic', { code: 'KeyM', ctrl: false, alt: false, shift: false, meta: false });
  for (const extra of [{ repeat: true }, { isComposing: true }, { closest: () => ({}) }]) {
    const e = event('keydown', extra);
    if ('closest' in extra)
      Object.defineProperty(e, 'target', { value: { closest: extra.closest } });
    win.dispatchEvent(e);
  }
  expect(action).not.toHaveBeenCalled();
  win.dispatchEvent(event('keydown'));
  expect(action).toHaveBeenCalledExactlyOnceWith('mic');
});
it('keeps bindings per profile, rejects duplicates and restores defaults', async () => {
  const user = ref('first'),
    keys = useHotkeys(user, () => {}),
    binding = { code: 'Digit4', ctrl: false, alt: false, shift: false, meta: false };
  expect(keys.set('mic', binding)).toBe(true);
  expect(keys.set('camera', binding)).toBe(false);
  keys.set('screen');
  user.value = 'second';
  await nextTick();
  expect(keys.bindings.value.mic?.code).toBe('KeyM');
  user.value = 'first';
  await nextTick();
  expect(keys.bindings.value.mic?.code).toBe('Digit4');
  expect(keys.bindings.value.screen).toBeUndefined();
  keys.reset();
  expect(keys.bindings.value.screen?.code).toBe('KeyS');
});
it('persists and runs punctuation, navigation and formerly reserved keys', async () => {
  const user = ref('first'),
    action = vi.fn(),
    keys = useHotkeys(user, action);
  for (const code of ['Home', 'Slash', 'Escape', 'Backspace', 'Delete', 'F9', 'Space']) {
    keys.set('camera', { code, ctrl: false, alt: false, shift: false, meta: false });
    user.value = 'second';
    await nextTick();
    user.value = 'first';
    await nextTick();
    expect(keys.bindings.value.camera?.code).toBe(code);
    const down = event('keydown', { code });
    win.dispatchEvent(down);
    expect(down.defaultPrevented).toBe(true);
  }
  expect(action).toHaveBeenCalledTimes(7);
});
it('runs standalone modifiers on release, never as a side effect of a chord or focus loss', () => {
  const action = vi.fn(),
    keys = useHotkeys(ref('test'), action);
  keys.set('camera', { code: 'ControlLeft', ctrl: false, alt: false, shift: false, meta: false });
  win.dispatchEvent(event('keydown', { code: 'ControlLeft', ctrlKey: true }));
  expect(action).not.toHaveBeenCalled();
  win.dispatchEvent(event('keyup', { code: 'ControlLeft' }));
  expect(action).toHaveBeenCalledExactlyOnceWith('camera');
  action.mockClear();
  for (const interruption of [
    event('keydown', { code: 'KeyX', ctrlKey: true }),
    event('blur'),
    event('mousedown', { button: 0 }),
  ]) {
    win.dispatchEvent(event('keydown', { code: 'ControlLeft', ctrlKey: true }));
    win.dispatchEvent(interruption);
    win.dispatchEvent(event('keyup', { code: 'ControlLeft' }));
  }
  expect(action).not.toHaveBeenCalled();
});
