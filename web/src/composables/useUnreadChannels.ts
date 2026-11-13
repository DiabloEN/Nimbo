import { computed, onBeforeUnmount, onMounted, ref, watch, type Ref } from 'vue';

type Activity = { id: string; createdAt: number };

export function useUnreadChannels(
  origin: Ref<string>,
  user: Ref<string>,
  ready: Ref<boolean>,
  activity: Ref<Record<string, Activity>>,
) {
  const prefix = computed(() =>
    ready.value && user.value ? `nimbo-read:${origin.value}:${user.value}:` : '',
  );
  const seen = ref<Record<string, Activity>>({});
  function save(channel: string, latest: Activity) {
    seen.value[channel] = latest;
    try {
      localStorage.setItem(prefix.value + 'channel:' + channel, JSON.stringify(latest));
    } catch {
      /* Keep read state in memory if storage is unavailable. */
    }
  }
  function load(channel: string) {
    try {
      const value = JSON.parse(localStorage.getItem(prefix.value + 'channel:' + channel) || 'null');
      if (value && typeof value.id === 'string' && Number.isFinite(value.createdAt))
        seen.value[channel] = value;
    } catch {
      /* Ignore an invalid local marker. */
    }
  }
  watch(
    prefix,
    (key) => {
      seen.value = {};
      if (!key) return;
      let firstVisit = true;
      try {
        firstVisit = !localStorage.getItem(key + 'initialized');
      } catch {}
      for (const [channel, latest] of Object.entries(activity.value)) {
        if (firstVisit) save(channel, latest);
        else load(channel);
      }
      try {
        localStorage.setItem(key + 'initialized', '1');
      } catch {}
    },
    { immediate: true },
  );
  const unread = computed(
    () =>
      new Set(
        Object.entries(activity.value)
          .filter(([channel, latest]) => {
            const last = seen.value[channel];
            return (
              prefix.value &&
              (!last || (latest.id !== last.id && latest.createdAt >= last.createdAt))
            );
          })
          .map(([channel]) => channel),
      ),
  );
  function markRead(channel: string, ids: string[]) {
    const latest = activity.value[channel];
    if (latest && unread.value.has(channel) && ids.includes(latest.id)) save(channel, latest);
  }
  function sync(event: StorageEvent) {
    const key = prefix.value + 'channel:';
    if (prefix.value && event.key?.startsWith(key)) load(event.key.slice(key.length));
  }
  onMounted(() => window.addEventListener('storage', sync));
  onBeforeUnmount(() => window.removeEventListener('storage', sync));
  return { unread, markRead };
}
