import { onMounted, onBeforeUnmount, watch, ref, type Ref } from 'vue';
import type { MentionNotice } from './mentions';

export function useMentionAlerts(
  items: Ref<MentionNotice[]>,
  userId: Ref<string>,
  ready: Ref<boolean>,
  activity: Ref<Record<string, { id: string; createdAt: number }>> = ref({}),
) {
  const subscriptions = ref<string[]>([]);
  watch(
    userId,
    (id) => {
      subscriptions.value = [];
      if (id)
        try {
          const saved = JSON.parse(localStorage.getItem('nimbo-chat-notifications:' + id) || '[]');
          if (Array.isArray(saved))
            subscriptions.value = saved.filter((v: unknown) => typeof v === 'string').slice(0, 256);
        } catch {}
    },
    { immediate: true, flush: 'sync' },
  );
  function toggle(channel: string) {
    if (!userId.value || !channel) return;
    subscriptions.value = subscriptions.value.includes(channel)
      ? subscriptions.value.filter((id) => id !== channel)
      : [...subscriptions.value, channel];
    try {
      localStorage.setItem(
        'nimbo-chat-notifications:' + userId.value,
        JSON.stringify(subscriptions.value),
      );
    } catch {}
  }
  let previousActivity: Record<string, { id: string; createdAt: number }> = {};
  let context: AudioContext | undefined,
    identity = '',
    initialized = false,
    latest = 0,
    lastSound = -Infinity;
  let seen = new Set<string>();
  let icon: HTMLLinkElement | null = null,
    originalHref: string | null = null;
  function badge(unread: boolean) {
    if (!icon) return;
    const href = unread ? '/favicon-unread.svg' : originalHref;
    if (href && icon.getAttribute('href') !== href) icon.setAttribute('href', href);
  }
  // Browsers allow audio after the first keyboard or pointer interaction.
  function unlock() {
    try {
      context ??= new AudioContext();
      if (context.state === 'suspended') void context.resume().catch(() => {});
    } catch {
      /* The visual notification remains available. */
    }
  }
  function chime() {
    const ctx = context,
      now = performance.now();
    if (!ctx || ctx.state !== 'running' || now - lastSound < 1500) return;
    lastSound = now;
    try {
      for (const [delay, frequency] of [
        [0, 660],
        [0.12, 880],
      ]) {
        const oscillator = ctx.createOscillator(),
          gain = ctx.createGain(),
          start = ctx.currentTime + delay!;
        oscillator.type = 'sine';
        oscillator.frequency.value = frequency!;
        gain.gain.setValueAtTime(0, start);
        gain.gain.linearRampToValueAtTime(0.045, start + 0.012);
        gain.gain.exponentialRampToValueAtTime(0.0001, start + 0.2);
        oscillator.connect(gain);
        gain.connect(ctx.destination);
        oscillator.start(start);
        oscillator.stop(start + 0.22);
        oscillator.onended = () => {
          oscillator.disconnect();
          gain.disconnect();
        };
      }
    } catch {
      /* Audio must never interrupt chat. */
    }
  }
  const stop = watch(
    [items, userId, ready, activity],
    ([next, id, loaded, activityNow]) => {
      badge(loaded && !!id && next.length > 0);
      if (!loaded || !id) {
        identity = '';
        initialized = false;
        latest = 0;
        seen.clear();
        previousActivity = {};
        return;
      }
      if (identity !== id) {
        identity = id;
        initialized = false;
        latest = 0;
        seen.clear();
        previousActivity = {};
      }
      // Restoring an existing inbox is silent. Polls, read acknowledgements and
      // older notices returning to the first page must not replay the sound.
      const fresh =
        initialized &&
        (next.some((n) => !seen.has(n.id) && n.createdAt >= latest) ||
          Object.entries(activityNow).some(
            ([channel, value]) =>
              subscriptions.value.includes(channel) &&
              value.id !== previousActivity[channel]?.id &&
              value.createdAt >= (previousActivity[channel]?.createdAt ?? 0),
          ));
      for (const [channel, value] of Object.entries(activityNow)) {
        if (value.createdAt >= (previousActivity[channel]?.createdAt ?? 0))
          previousActivity[channel] = value;
      }
      latest = Math.max(latest, ...next.map((n) => n.createdAt));
      seen = new Set([...seen, ...next.map((n) => n.id)]);
      if (seen.size > 1000) seen = new Set(next.map((n) => n.id));
      initialized = true;
      if (fresh) chime();
    },
    { flush: 'post', immediate: true },
  );
  onMounted(() => {
    icon = document.querySelector<HTMLLinkElement>('link[rel="icon"]');
    originalHref = icon?.getAttribute('href') ?? null;
    badge(ready.value && !!userId.value && items.value.length > 0);
    document.addEventListener('pointerdown', unlock);
    document.addEventListener('keydown', unlock);
  });
  onBeforeUnmount(() => {
    stop();
    badge(false);
    document.removeEventListener('pointerdown', unlock);
    document.removeEventListener('keydown', unlock);
    if (context) void context.close().catch(() => {});
  });
  return { subscriptions, toggle };
}
