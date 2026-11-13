import { computed, ref, watch, type Ref } from 'vue';

type Gesture = 'move' | 'resize';
const clamp = (value: number, max: number) => Math.max(0, Math.min(value, max));

export function useMiniPlayer(active: Ref<boolean>) {
  const bounds = ref({ left: 0, top: 0, width: 0, height: 0 });
  const offset = ref({ right: 0, top: 0 });
  const preferredWidth = ref(320);
  const box = computed(() => {
    const b = bounds.value;
    const chrome = 54;
    const maxWidth = Math.max(0, Math.min(b.width, (b.height - chrome) * 16 / 9 + 16));
    const width = Math.min(maxWidth, Math.max(180, preferredWidth.value));
    const height = Math.min(b.height, chrome + Math.max(0, width - 16) * 9 / 16);
    const right = clamp(offset.value.right, b.width - width);
    const top = clamp(offset.value.top, b.height - height);
    return { width, height, right, top, maxWidth };
  });
  const style = computed(() => {
    const r = box.value, b = bounds.value;
    return {
      right: 'auto', left: b.left + b.width - r.right - r.width + 'px',
      top: b.top + r.top + 'px', width: r.width + 'px', height: r.height + 'px',
      visibility: (r.width && r.height ? 'visible' : 'hidden') as 'visible' | 'hidden',
    };
  });
  let releasePointer: (() => void) | undefined;
  watch(active, (enabled, _, cleanup) => {
    if (!enabled) return;
    const main = document.querySelector('.main-column');
    if (!main) return;
    let observed: Element | undefined;
    const resize = new ResizeObserver(measure);
    function measure() {
      if (!main) return;
      const area = [...main.querySelectorAll('.chat-messages, .room-stage, .settings-page, .profile-page')]
        .filter(el => el.getClientRects().length)
        .sort((a,b) => Number(b.matches('.chat-messages')) - Number(a.matches('.chat-messages')))[0] || main;
      if (area !== observed) {
        if (observed && observed !== main) resize.unobserve(observed);
        resize.observe(area);
        observed = area;
      }
      const rect = area.getBoundingClientRect();
      const left = Math.max(0, rect.left) + 6, top = Math.max(0, rect.top) + 6;
      bounds.value = { left, top, width: Math.max(0, Math.min(innerWidth, rect.right) - left - 6),
        height: Math.max(0, Math.min(innerHeight, rect.bottom) - top - 6) };
    }
    resize.observe(main);
    const route = new MutationObserver(measure);
    route.observe(main, { attributes: true, attributeFilter: ['class'], childList: true });
    window.addEventListener('resize', measure);
    window.addEventListener('scroll', measure, true);
    measure();
    cleanup(() => {
      releasePointer?.();
      resize.disconnect();
      route.disconnect();
      window.removeEventListener('resize', measure);
      window.removeEventListener('scroll', measure, true);
    });
  }, { flush: 'post' });
  function resizeFromCorner(initial: typeof box.value, delta: number) {
    const availableHeight = bounds.value.height - initial.top;
    const maxWidth = Math.min(initial.maxWidth, Math.max(0, availableHeight - 54) * 16 / 9 + 16);
    const width = Math.min(maxWidth, Math.max(180, initial.width + delta));
    preferredWidth.value = width;
    // Keep the opposite corner still until the player reaches the right chat boundary.
    offset.value = { right: Math.max(0, initial.right + initial.width - width), top: initial.top };
  }
  function grow(delta: number) {
    resizeFromCorner(box.value, delta);
  }
  function start(event: PointerEvent, kind: Gesture) {
    if (!active.value || event.button !== 0 || !event.isPrimary) return;
    releasePointer?.();
    const target = event.currentTarget as HTMLElement, initial = box.value;
    const id = event.pointerId;
    function move(e: PointerEvent) {
      if (e.pointerId !== id) return;
      const dx = e.clientX - event.clientX, dy = e.clientY - event.clientY;
      if (kind === 'move') offset.value = {
        right: clamp(initial.right - dx, bounds.value.width - box.value.width),
        top: clamp(initial.top + dy, bounds.value.height - box.value.height),
      };
      else {
        const growth = Math.abs(dx) >= Math.abs(dy) ? dx : dy * 16 / 9;
        resizeFromCorner(initial, growth);
      }
    }
    const end = (e: PointerEvent) => { if (e.pointerId === id) releasePointer?.(); };
    releasePointer = () => {
      target.removeEventListener('pointermove', move);
      for (const name of ['pointerup', 'pointercancel', 'lostpointercapture'] as const)
        target.removeEventListener(name, end);
      if (target.hasPointerCapture(id)) target.releasePointerCapture(id);
      releasePointer = undefined;
    };
    target.setPointerCapture(id);
    target.addEventListener('pointermove', move);
    for (const name of ['pointerup', 'pointercancel', 'lostpointercapture'] as const)
      target.addEventListener(name, end);
  }
  return { start, grow, style };
}
