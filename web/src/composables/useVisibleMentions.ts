import { watch, onMounted, onBeforeUnmount, type Ref } from 'vue';

// Read an alert only when its tag is actually visible in the active chat.
export function useVisibleMentions(
  list: Ref<HTMLElement | undefined>,
  state: () => { active: boolean; ids: string[]; channel: string; count: number },
  read: (ids: string[]) => void,
) {
  let observer: IntersectionObserver | undefined, timer: ReturnType<typeof setTimeout> | undefined;
  const visible = new Set<string>();
  function reset() {
    observer?.disconnect();
    clearTimeout(timer);
    visible.clear();
  }
  function observe() {
    reset();
    const s = state();
    if (!s.active || document.hidden || !list.value || !s.ids.length) return;
    observer = new IntersectionObserver(
      (entries) => {
        for (const e of entries) {
          const id = (e.target as HTMLElement).dataset.mentionReadId!;
          if (e.isIntersecting) visible.add(id);
          else visible.delete(id);
        }
        clearTimeout(timer);
        if (visible.size)
          timer = setTimeout(() => {
            if (state().active && !document.hidden && state().channel === s.channel)
              read([...visible]);
          }, 700);
      },
      { root: list.value, threshold: 0.5 },
    );
    for (const article of list.value.querySelectorAll<HTMLElement>('[data-message-id]')) {
      const id = article.dataset.messageId!;
      if (!s.ids.includes(id)) continue;
      const tag = article.querySelector<HTMLElement>('.personal-mention') ?? article;
      tag.dataset.mentionReadId = id;
      observer.observe(tag);
    }
  }
  watch(state, observe, { flush: 'post', deep: true });
  onMounted(() => {
    document.addEventListener('visibilitychange', observe);
    observe();
  });
  onBeforeUnmount(() => {
    reset();
    document.removeEventListener('visibilitychange', observe);
  });
}
