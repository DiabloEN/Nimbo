<script setup lang="ts">
import { computed, onMounted, onBeforeUnmount, ref, watch } from 'vue';
const props = defineProps<{
  href: string;
  source: string;
  channel: string;
  message: string;
  request: (path: string) => Promise<any>;
}>();
type Card = { title?: string; description?: string; image?: string; price?: string };
const root = ref<HTMLElement>();
const card = ref<Card>({});
const playing = ref(false);
const visible = ref(false);
const url = computed(() => new URL(props.href));
const youtube = computed(() => {
  const { hostname, pathname, searchParams } = url.value;
  const parts = pathname.split('/').filter(Boolean);
  let id = '';
  if (hostname === 'youtu.be') id = parts[0] || '';
  if (['www.youtube.com', 'm.youtube.com', 'youtube.com'].includes(hostname))
    id =
      pathname === '/watch'
        ? searchParams.get('v') || ''
        : ['embed', 'shorts', 'live'].includes(parts[0] || '')
          ? parts[1] || ''
          : '';
  return /^[\w-]{11}$/.test(id) ? id : '';
});
let observer: IntersectionObserver;
onMounted(() => {
  observer = new IntersectionObserver(
    (entries) => {
      if (entries.some((e) => e.isIntersecting)) {
        visible.value = true;
        observer.disconnect();
      }
    },
    { rootMargin: '160px' },
  );
  observer.observe(root.value!);
});
onBeforeUnmount(() => observer?.disconnect());
watch(
  () => [visible.value, props.href, props.channel, props.message],
  async (_, __, cleanup) => {
    let current = true;
    cleanup(() => {
      current = false;
    });
    card.value = {};
    playing.value = false;
    if (!visible.value) return;
    const q = new URLSearchParams({
      url: props.source,
      channel: props.channel,
      message: props.message,
    });
    try {
      const result = await props.request('/api/link-preview?' + q);
      if (current) card.value = result;
    } catch {
      /* The original link remains usable when the publisher declines preview requests. */
    }
  },
);
</script>
<template>
  <div ref="root" class="preview-anchor">
    <div v-if="youtube || card.title" class="link-card">
      <iframe
        v-if="playing && youtube"
        :src="'https://www.youtube-nocookie.com/embed/' + youtube + '?autoplay=1'"
        title="Видео YouTube"
        referrerpolicy="strict-origin-when-cross-origin"
        allow="autoplay; fullscreen; picture-in-picture"
        allowfullscreen
      />
      <button
        v-else-if="youtube"
        type="button"
        class="video-preview"
        :aria-label="
          card.title ? 'Воспроизвести видео: ' + card.title : 'Воспроизвести видео YouTube'
        "
        @click="playing = true"
      >
        <img v-if="card.image" alt="" :src="card.image" />
        <span class="video-title">{{ card.title || 'Видео YouTube' }}</span>
        <span class="video-play" aria-hidden="true">▶</span>
      </button>
      <a v-else :href="href" target="_blank" rel="noopener noreferrer">
        <img v-if="card.image" alt="" :src="card.image" />
        <div>
          <small>{{ url.hostname }}</small
          ><strong>{{ card.title }}</strong> <span v-if="card.price">{{ card.price }}</span>
          <p v-if="card.description">{{ card.description }}</p>
        </div>
      </a>
    </div>
  </div>
</template>
<style scoped>
.preview-anchor {
  min-height: 1px;
}
.link-card {
  width: min(100%, 420px);
  margin-top: 7px;
  border-radius: 13px;
  overflow: hidden;
  background: #1d1d1d;
}
.link-card a {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px;
  text-align: left;
  color: inherit;
  text-decoration: none;
}
.video-preview {
  position: relative;
  display: grid;
  place-items: center;
  aspect-ratio: 16 / 9;
  width: 100%;
  padding: 0;
  border: 0;
  border-radius: inherit;
  overflow: hidden;
  background: #191919;
  color: white;
  cursor: pointer;
}
.video-preview img {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.video-title {
  position: absolute;
  inset: 0 0 auto;
  padding: 12px 14px 22px;
  background: linear-gradient(to bottom, rgb(0 0 0 / 82%), transparent);
  text-align: left;
  text-overflow: ellipsis;
  overflow: hidden;
  white-space: nowrap;
  font-weight: 600;
}
.video-play {
  position: relative;
  padding: 9px 17px 9px 19px;
  border-radius: 12px;
  background: rgb(0 0 0 / 76%);
  font-size: 21px;
  line-height: 1;
}
.video-preview:hover .video-play,
.video-preview:focus-visible .video-play {
  background: #111;
}
.video-preview:focus-visible {
  outline: 2px solid white;
  outline-offset: -3px;
}
.link-card a img {
  width: 82px;
  max-height: 96px;
  object-fit: cover;
  border-radius: 8px;
}
.link-card strong,
.link-card small,
.link-card span {
  display: block;
}
.link-card p,
.link-card small {
  margin: 3px 0;
  color: var(--quiet);
}
.link-card iframe {
  width: 100%;
  aspect-ratio: 16/9;
  display: block;
  border: 0;
}
</style>
