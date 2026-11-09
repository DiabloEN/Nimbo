import { ref, watch, onMounted, onBeforeUnmount, type Ref } from 'vue';
export const hotkeyActions = {
  mic: 'Микрофон',
  camera: 'Камера',
  screen: 'Показ экрана',
  members: 'Панель участников',
  effects: 'Размытие фона',
  settings: 'Настройки звука',
};
export type HotkeyAction = keyof typeof hotkeyActions;
export type Binding = { code: string; ctrl: boolean; alt: boolean; shift: boolean; meta: boolean };
export type Bindings = Partial<Record<HotkeyAction, Binding>>;
export const defaults: Bindings = Object.fromEntries(
  Object.keys(hotkeyActions).map((a, i) => [
    a,
    {
      code: ['KeyM', 'KeyV', 'KeyS', 'KeyP', 'KeyB', 'KeyO'][i],
      ctrl: true,
      alt: true,
      shift: false,
      meta: false,
    },
  ]),
);
export const modifierKey = (code: string) => /^(Control|Alt|Shift|Meta)(Left|Right)$/.test(code);
const keyboardCode = (code: unknown): code is string =>
  typeof code === 'string' &&
  /^[A-Za-z][A-Za-z0-9]{0,63}$/.test(code) &&
  code !== 'Unidentified' &&
  !code.startsWith('Mouse');
export function readBinding(
  e: Pick<
    KeyboardEvent,
    'code' | 'ctrlKey' | 'altKey' | 'shiftKey' | 'metaKey' | 'getModifierState'
  >,
): Binding | undefined {
  if (!keyboardCode(e.code) || e.getModifierState('AltGraph')) return;
  return {
    code: e.code,
    ctrl: e.ctrlKey && !e.code.startsWith('Control'),
    alt: e.altKey && !e.code.startsWith('Alt'),
    shift: e.shiftKey && !e.code.startsWith('Shift'),
    meta: e.metaKey && !e.code.startsWith('Meta'),
  };
}
export function readMouseBinding(
  e: Pick<MouseEvent, 'button' | 'ctrlKey' | 'altKey' | 'shiftKey' | 'metaKey'>,
): Binding | undefined {
  if (e.button !== 3 && e.button !== 4) return;
  return {
    code: 'Mouse' + (e.button + 1),
    ctrl: e.ctrlKey,
    alt: e.altKey,
    shift: e.shiftKey,
    meta: e.metaKey,
  };
}
export function sameBinding(a: Binding | undefined, b: Binding | undefined) {
  return (
    !!a &&
    !!b &&
    a.code === b.code &&
    a.ctrl === b.ctrl &&
    a.alt === b.alt &&
    a.shift === b.shift &&
    a.meta === b.meta
  );
}
const keyLabels: Record<string, string> = {
  Slash: '/',
  Backslash: '\\',
  Semicolon: ';',
  Quote: "'",
  Backquote: '`',
  BracketLeft: '[',
  BracketRight: ']',
  Minus: '−',
  Equal: '=',
  Comma: ',',
  Period: '.',
  Space: 'Пробел',
  ArrowUp: '↑',
  ArrowDown: '↓',
  ArrowLeft: '←',
  ArrowRight: '→',
  ControlLeft: 'Левый Ctrl',
  ControlRight: 'Правый Ctrl',
  AltLeft: 'Левый Alt',
  AltRight: 'Правый Alt',
  ShiftLeft: 'Левый Shift',
  ShiftRight: 'Правый Shift',
  MetaLeft: 'Левый ⌘',
  MetaRight: 'Правый ⌘',
};
export function bindingLabel(b?: Binding) {
  return b
    ? [
        b.ctrl ? 'Ctrl' : '',
        b.alt ? 'Alt' : '',
        b.shift ? 'Shift' : '',
        b.meta ? '⌘' : '',
        keyLabels[b.code] ??
          b.code
            .replace(/^Mouse/, 'Мышь ')
            .replace(/^Numpad/, 'Num ')
            .replace(/^(Key|Digit)/, ''),
      ]
        .filter(Boolean)
        .join(' + ')
    : 'Не назначено';
}
function valid(b: unknown): b is Binding {
  const v = b as Binding;
  return (
    !!v &&
    (keyboardCode(v.code) || /^Mouse[45]$/.test(v.code)) &&
    ['ctrl', 'alt', 'shift', 'meta'].every((k) => typeof v[k as keyof Binding] === 'boolean')
  );
}
export function parseBindings(value: unknown): Bindings {
  if (!value || typeof value !== 'object' || Array.isArray(value))
    throw new Error('Некорректные горячие клавиши в профиле.');
  const result: Bindings = {};
  for (const id of Object.keys(hotkeyActions) as HotkeyAction[]) {
    const binding = (value as Record<string, unknown>)[id];
    if (binding === undefined) continue;
    if (!valid(binding) || Object.values(result).some((other) => sameBinding(other, binding)))
      throw new Error('Некорректные горячие клавиши в профиле.');
    result[id] = {
      code: binding.code,
      ctrl: binding.ctrl,
      alt: binding.alt,
      shift: binding.shift,
      meta: binding.meta,
    };
  }
  return result;
}
export function hotkeyInput(target: EventTarget | null) {
  return !!(target as HTMLElement)?.closest?.(
    'input,textarea,select,[contenteditable]:not([contenteditable="false"]),[role="textbox"],[data-hotkey-capture]',
  );
}
export function useHotkeys(userId: Ref<string>, action: (id: HotkeyAction) => void) {
  const bindings = ref<Bindings>({ ...defaults }),
    note = ref('');
  watch(
    userId,
    (id) => {
      bindings.value = { ...defaults };
      note.value = '';
      if (!id) return;
      try {
        const raw = localStorage.getItem('nimbo-hotkeys:' + id);
        if (raw) {
          const data = JSON.parse(raw);
          bindings.value = {};
          for (const id of Object.keys(hotkeyActions) as HotkeyAction[])
            if (valid(data[id])) bindings.value[id] = data[id];
        }
      } catch {
        note.value = 'Не удалось прочитать сочетания.';
      }
    },
    { immediate: true },
  );
  function save(next: Bindings) {
    bindings.value = next;
    try {
      localStorage.setItem('nimbo-hotkeys:' + userId.value, JSON.stringify(next));
      note.value = '';
    } catch {
      note.value = 'Не удалось сохранить сочетания в браузере.';
    }
  }
  function set(id: HotkeyAction, b?: Binding) {
    const duplicate = (Object.keys(bindings.value) as HotkeyAction[]).find(
      (k) => k !== id && sameBinding(bindings.value[k], b),
    );
    if (duplicate) {
      note.value = 'Это сочетание уже назначено: ' + hotkeyActions[duplicate];
      return false;
    }
    save({ ...bindings.value, [id]: b });
    return true;
  }
  function match(b?: Binding) {
    return (Object.keys(bindings.value) as HotkeyAction[]).find((k) =>
      sameBinding(bindings.value[k], b),
    );
  }
  let pendingModifier: Binding | undefined;
  function key(e: KeyboardEvent) {
    pendingModifier = undefined;
    if (!userId.value || e.defaultPrevented || e.repeat || e.isComposing || hotkeyInput(e.target))
      return;
    const b = readBinding(e);
    if (modifierKey(e.code)) {
      pendingModifier = b;
      return;
    }
    const id = match(b);
    if (id) {
      e.preventDefault();
      action(id);
    }
  }
  function keyRelease(e: KeyboardEvent) {
    const b = pendingModifier;
    pendingModifier = undefined;
    if (
      !b ||
      b.code !== e.code ||
      !userId.value ||
      e.defaultPrevented ||
      e.isComposing ||
      hotkeyInput(e.target)
    )
      return;
    const id = match(b);
    if (id) {
      e.preventDefault();
      action(id);
    }
  }
  function clearModifier() {
    pendingModifier = undefined;
  }
  let consumedButton = -1,
    releaseTimer: ReturnType<typeof setTimeout> | undefined;
  function mouse(e: MouseEvent) {
    pendingModifier = undefined;
    // Embedded browsers may cancel Back/Forward before this handler. The
    // assigned side button still performs its action; capture controls stop
    // propagation explicitly and editable elements remain excluded.
    if (!userId.value || hotkeyInput(e.target) || hotkeyInput(document.activeElement)) return;
    const id = match(readMouseBinding(e));
    if (id) {
      e.preventDefault();
      consumedButton = e.button;
      clearTimeout(releaseTimer);
      releaseTimer = setTimeout(() => (consumedButton = -1), 1500);
      action(id);
    }
  }
  function release(e: MouseEvent) {
    if (e.button === consumedButton) e.preventDefault();
  }
  onMounted(() => {
    window.addEventListener('keydown', key);
    window.addEventListener('keyup', keyRelease);
    window.addEventListener('blur', clearModifier);
    window.addEventListener('mousedown', mouse);
    window.addEventListener('mouseup', release);
    window.addEventListener('auxclick', release);
  });
  onBeforeUnmount(() => {
    window.removeEventListener('keydown', key);
    window.removeEventListener('keyup', keyRelease);
    window.removeEventListener('blur', clearModifier);
    window.removeEventListener('mousedown', mouse);
    window.removeEventListener('mouseup', release);
    window.removeEventListener('auxclick', release);
    clearTimeout(releaseTimer);
  });
  return {
    bindings,
    note,
    set,
    replace: (value: Bindings) => save(parseBindings(value)),
    reset: () => save({ ...defaults }),
  };
}
