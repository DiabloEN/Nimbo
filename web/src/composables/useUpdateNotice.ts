import { computed, onMounted, onBeforeUnmount, ref, watch, type Ref } from 'vue';

export type ServerUpdateStatus = {
  installed: string;
  available: string;
  previous?: string;
  phase: string;
  canInstall: boolean;
  checkedAt?: number;
  detail?: string;
};

export function useUpdateNotice(
  key: Readonly<Ref<string>>,
  panelOpen: Readonly<Ref<boolean>>,
  load: (signal: AbortSignal) => Promise<ServerUpdateStatus>,
) {
  const available = ref('');
  const seen = ref('');
  const visible = ref(false);
  const unread = computed(() => !!available.value && available.value !== seen.value);

  function accept(status: ServerUpdateStatus) {
    if (!key.value) return;
    if (status.available && status.available === status.installed) {
      available.value = '';
    } else if (status.canInstall && status.available) {
      available.value = status.available;
    } else if (!['checking', 'queued', 'waiting', 'installing', 'error'].includes(status.phase)) {
      available.value = '';
    }
  }

  function markSeen(tag: string) {
    if (!key.value || !tag || tag !== available.value || tag === seen.value) return;
    seen.value = tag;
    try {
      localStorage.setItem(key.value, tag);
    } catch {}
  }

  watch(
    key,
    () => {
      available.value = '';
      seen.value = '';
      try {
        if (key.value) seen.value = localStorage.getItem(key.value) || '';
      } catch {}
    },
    { immediate: true, flush: 'sync' },
  );

  // The open panel already polls this endpoint and reports its responses to accept().
  watch(
    [key, panelOpen, visible],
    ([scope, open, shown], _, cleanup) => {
      if (!scope || open || !shown) return;
      const controller = new AbortController();
      let timer: ReturnType<typeof setTimeout>;
      async function poll() {
        try {
          const result = await load(controller.signal);
          if (!controller.signal.aborted) accept(result);
        } catch {
          // Keep the last known release across temporary connection failures.
        }
        if (!controller.signal.aborted) timer = setTimeout(poll, 60_000);
      }
      cleanup(() => {
        controller.abort();
        clearTimeout(timer);
      });
      void poll();
    },
    { flush: 'sync' },
  );

  function visibility() {
    visible.value = document.visibilityState === 'visible';
  }
  function storage(event: StorageEvent) {
    if (event.key === key.value || event.key === null) seen.value = event.newValue || '';
  }
  onMounted(() => {
    visibility();
    document.addEventListener('visibilitychange', visibility);
    window.addEventListener('storage', storage);
  });
  onBeforeUnmount(() => {
    document.removeEventListener('visibilitychange', visibility);
    window.removeEventListener('storage', storage);
  });
  return { available, unread, visible, accept, markSeen };
}
