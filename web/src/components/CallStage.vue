<script setup lang="ts">
import { computed, ref, watch, nextTick, onBeforeUnmount } from 'vue';
import { useMiniPlayer } from '../composables/useMiniPlayer';
import AppIcon from './AppIcon.vue';
import ScreenAudioButton from './ScreenAudioButton.vue';
import type { VideoTile } from '../composables/useCall';

type Person = {
  identity: string;
  name: string;
  local: boolean;
  ownProfile?: boolean;
  speaking: boolean;
  mic: boolean;
  avatar?: string;
  reconnecting?: boolean;
  canModerateMedia?: boolean;
};
const emit = defineEmits<{
  leave: [];
  'toggle-screen-audio': [];
  'toggle-screen-playback': [string];
  'person-menu': [Person, MouseEvent?];
  'moderate-media': [Person, string];
}>();
const props = defineProps<{
  people: Person[];
  tiles: VideoTile[];
  visible: boolean;
  followRemoteScreen?: boolean;
  moderationBusy?: boolean;
  mutedScreens?: Set<string>;
  audibleScreens?: Set<string>;
  screenAudio?: boolean;
  screenAudioAvailable?: boolean;
  screenAudioBusy?: boolean;
  bindVideoHost: (id: string, host: HTMLElement | null) => void;
  refreshVideoLayout: () => void;
}>();
const floating = ref<HTMLElement | null>(null),
  enlargedHost = ref<HTMLElement | null>(null),
  expandedId = ref(''),
  minimized = ref(false);
const mini = useMiniPlayer(minimized);
const hosts = new Map<string, HTMLElement>();
const expanded = computed(() => props.tiles.find((t) => t.id === expandedId.value));
const cameraFor = (id: string) =>
  props.tiles.find((t) => t.participantId === id && t.source === 'camera');
const camerasFor = (id: string) =>
  props.tiles.filter((t) => t.participantId === id && t.source === 'camera').slice(0, 1);
const screensFor = (id: string) =>
  props.tiles.filter((t) => t.participantId === id && t.source === 'screen');
function initials(name: string) {
  return name.trim().slice(0, 2).toUpperCase();
}
function host(id: string, element: unknown) {
  const node = element as HTMLElement | null;
  if (node) hosts.set(id, node);
  else hosts.delete(id);
  if (expandedId.value !== id) props.bindVideoHost(id, node);
}
function enlarge(tile: VideoTile, focus = true) {
  if (!enlargedHost.value) return;
  const previous = expandedId.value;
  if (previous && previous !== tile.id) props.bindVideoHost(previous, hosts.get(previous) || null);
  expandedId.value = tile.id;
  minimized.value = false;
  // Reuse the same video element/subscription in the large and floating views.
  props.bindVideoHost(tile.id, enlargedHost.value);
  void nextTick(() => {
    props.refreshVideoLayout();
    if (focus) floating.value?.querySelector<HTMLButtonElement>('header button')?.focus();
  });
}
function minimize(focus = false) {
  minimized.value = true;
  void nextTick(() => {
    props.refreshVideoLayout();
    if (focus) floating.value?.querySelector<HTMLButtonElement>('header button')?.focus();
  });
}
function close() {
  const id = expandedId.value;
  expandedId.value = '';
  minimized.value = false;
  if (id) props.bindVideoHost(id, hosts.get(id) || null);
  void nextTick(props.refreshVideoLayout);
}
watch(
  () => props.visible,
  (visible, previous) => {
    if (!visible && previous) {
      if (!expandedId.value) {
        const screen = props.tiles.find((t) => t.source === 'screen' && !t.local);
        if (screen) enlarge(screen, false);
      }
      if (expandedId.value) minimize();
    } else if (visible && minimized.value) close();
  },
);
watch(
  () => props.tiles.map((t) => t.id).join(','),
  () => {
    if (expandedId.value && !expanded.value) close();
  },
);
watch(
  () =>
    props.followRemoteScreen
      ? props.tiles.find((t) => t.source === 'screen' && !t.local)?.id
      : undefined,
  (id) => {
    const screen = id && props.tiles.find((t) => t.id === id);
    if (screen && !expandedId.value) {
      enlarge(screen, false);
      minimize();
    }
  },
);
onBeforeUnmount(() => {
  close();
  for (const id of hosts.keys()) props.bindVideoHost(id, null);
});
</script>

<template>
  <div class="call-grid">
    <div
      v-for="person in people"
      :key="person.identity"
      class="participant-group"
      :class="{ 'has-screen': screensFor(person.identity).length > 0 }"
    >
      <article
        class="person-card"
        :class="{ speaking: person.speaking, 'with-camera': !!cameraFor(person.identity) }"
        @contextmenu.prevent="emit('person-menu', person, $event)"
      >
        <button
          v-if="person.local"
          class="person-leave"
          aria-label="Выйти из голосовой комнаты"
          title="Выйти из комнаты"
          @click="emit('leave')"
        >
          <AppIcon name="exit" :size="18" /></button
        ><span v-if="person.reconnecting || person.speaking || !person.mic" class="person-state"
          ><span
            v-if="person.reconnecting"
            class="reconnecting-dot"
            aria-label="Переподключается"
            title="Переподключается"
          ></span
          ><span v-else-if="person.speaking" class="wave" aria-label="Говорит"
            ><i></i><i></i><i></i><i></i></span
          ><AppIcon v-else-if="!person.mic" name="mic-off" :size="14"
        /></span>
        <template v-if="cameraFor(person.identity)"
          ><div
            v-for="tile in camerasFor(person.identity)"
            :key="tile.id"
            :ref="(el) => host(tile.id, el)"
            class="media-slot camera-slot"
            :aria-label="`Камера: ${person.name}`"
          ></div
        ></template>
        <div v-else-if="screensFor(person.identity).length" class="camera-placeholder">
          <div class="avatar">
            <img v-if="person.avatar" :src="person.avatar" alt="" /><template v-else>{{
              initials(person.name)
            }}</template>
          </div>
        </div>
        <div v-else class="avatar">
          <img v-if="person.avatar" :src="person.avatar" alt="" /><template v-else>{{
            initials(person.name)
          }}</template>
        </div>
        <button
          v-if="!person.local"
          class="participant-volume"
          :aria-label="`Громкость: ${person.name}`"
          @click="emit('person-menu', person, $event)"
        >
          <AppIcon name="headphones" :size="16" />
        </button>
        <div v-if="person.canModerateMedia" class="media-admin-actions">
          <button
            v-if="person.mic"
            type="button"
            :disabled="moderationBusy"
            :aria-label="`Выключить микрофон: ${person.name}`"
            title="Выключить микрофон участника"
            @click="emit('moderate-media', person, 'mute_mic')"
          >
            <AppIcon name="mic-off" :size="16" /></button
          ><button
            v-if="cameraFor(person.identity)"
            type="button"
            :disabled="moderationBusy"
            :aria-label="`Выключить камеру: ${person.name}`"
            title="Выключить камеру участника"
            @click="emit('moderate-media', person, 'mute_camera')"
          >
            <AppIcon name="camera-off" :size="16" />
          </button>
        </div>
        <div class="person-caption">
          <strong
            >{{ person.name }}<span v-if="person.local || person.ownProfile"> · вы</span></strong
          >
        </div>
      </article>
      <article v-for="tile in screensFor(person.identity)" :key="tile.id" class="screen-card">
        <div
          :ref="(el) => host(tile.id, el)"
          class="media-slot screen-slot"
          :style="{ aspectRatio: tile.aspectRatio || 16 / 9 }"
          :aria-label="`Экран: ${person.name}`"
        ></div>
        <div v-if="person.canModerateMedia" class="media-admin-actions screen-admin-actions">
          <button
            type="button"
            :disabled="moderationBusy"
            :aria-label="`Остановить демонстрацию: ${person.name}`"
            title="Остановить демонстрацию участника"
            @click="emit('moderate-media', person, 'mute_screen')"
          >
            <AppIcon name="screen-off" :size="18" />
          </button>
        </div>
        <div class="screen-caption">
          <AppIcon name="screen" :size="16" /><span>{{ person.name }} · экран</span
          ><ScreenAudioButton
            v-if="tile.local"
            :enabled="screenAudio"
            :available="screenAudioAvailable"
            :busy="screenAudioBusy"
            @toggle="emit('toggle-screen-audio')"
          />
        </div>
        <button
          class="expand-screen"
          :aria-label="`Развернуть экран: ${person.name}`"
          title="Развернуть экран"
          @click="enlarge(tile)"
        >
          <AppIcon name="expand" :size="18" /><span>Развернуть</span>
        </button>
      </article>
    </div>
  </div>
  <Teleport to="body">
    <section
      ref="floating"
      v-show="expanded"
      class="stream-viewer"
      :class="{ 'stream-mini': minimized }"
      :style="minimized ? mini.style.value : undefined"
      role="region"
      aria-label="Просмотр трансляции"
      @keydown.esc.stop.prevent="minimized ? close() : minimize(true)"
    >
      <header>
        <h2 :class="{ 'drag-title': minimized }" @pointerdown="mini.start($event, 'move')">
          {{ expanded?.name }} · экран
        </h2>
        <button
          v-if="expanded && !expanded.local && audibleScreens?.has(expanded.participantId)"
          type="button"
          class="icon-button"
          aria-label="Заглушить звук трансляции у себя"
          :aria-pressed="mutedScreens?.has(expanded.participantId)"
          :title="
            mutedScreens?.has(expanded.participantId)
              ? 'Включить звук у себя'
              : 'Выключить звук у себя'
          "
          @click="emit('toggle-screen-playback', expanded.participantId)"
        >
          <AppIcon
            :name="mutedScreens?.has(expanded.participantId) ? 'sound-off' : 'sound'"
            :size="16"
          />
        </button>
        <button
          v-if="minimized && expanded?.local && screenAudioAvailable"
          type="button"
          class="icon-button"
          :disabled="screenAudioBusy"
          :aria-pressed="!!screenAudio"
          :aria-label="
            screenAudio ? 'Выключить звук своей трансляции' : 'Включить звук своей трансляции'
          "
          :title="
            screenAudio ? 'Выключить звук своей трансляции' : 'Включить звук своей трансляции'
          "
          @click="emit('toggle-screen-audio')"
        >
          <AppIcon :name="screenAudio ? 'sound' : 'sound-off'" :size="16" />
        </button>
        <button
          v-if="minimized"
          class="icon-button"
          aria-label="Развернуть трансляцию"
          title="Развернуть"
          @click="expanded && enlarge(expanded)"
        >
          <AppIcon name="expand" :size="18" /></button
        ><button
          v-else
          class="icon-button"
          aria-label="Свернуть трансляцию"
          title="Свернуть в мини-плеер"
          @click="minimize(true)"
        >
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" aria-hidden="true">
            <path d="M5 17h14" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" />
          </svg></button
        ><button
          class="icon-button"
          aria-label="Закрыть просмотр трансляции"
          title="Закрыть просмотр"
          @click="close"
        >
          <AppIcon name="close" :size="18" />
        </button>
      </header>
      <div class="stream-viewer-body" @pointerdown="mini.start($event, 'move')">
        <div ref="enlargedHost" class="media-slot enlarged-slot"></div>
      </div>
      <button
        v-if="minimized"
        type="button"
        class="mini-resize"
        aria-label="Изменить размер трансляции"
        title="Изменить размер"
        @pointerdown="mini.start($event, 'resize')"
        @keydown.left.prevent="mini.grow(-16)"
        @keydown.right.prevent="mini.grow(16)"
      >
        <svg width="14" height="14" viewBox="0 0 14 14" fill="none" aria-hidden="true">
          <path
            d="m3 11 8-8m-3 8 3-3"
            stroke="currentColor"
            stroke-width="1.5"
            stroke-linecap="round"
          />
        </svg>
      </button>
      <div v-if="expanded?.local && !minimized" class="screen-viewer-footer">
        <ScreenAudioButton
          :enabled="screenAudio"
          :available="screenAudioAvailable"
          :busy="screenAudioBusy"
          @toggle="emit('toggle-screen-audio')"
        />
      </div>
    </section>
  </Teleport>
</template>
<style scoped>
.person-leave {
  position: absolute;
  top: 10px;
  left: 10px;
  z-index: 5;
  display: grid;
  place-items: center;
  width: 32px;
  height: 32px;
  padding: 0;
  border-radius: 9px;
  background: #302124;
  color: #efaaaa;
  opacity: 0;
  transition: opacity 140ms;
}
.person-card:hover .person-leave,
.person-card:focus-within .person-leave {
  opacity: 1;
}
.person-leave:hover {
  background: #452a2e;
}
@media (hover: none) {
  .person-leave {
    opacity: 1;
  }
}

.participant-group.has-screen {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  width: min(100%, 792px);
  padding: 6px;
  gap: 8px;
  border-radius: 22px;
  background: #15191b;
  align-items: start;
}
.participant-group.has-screen .person-card,
.participant-group.has-screen .person-card.with-camera,
.participant-group.has-screen .screen-card {
  width: 100%;
  min-width: 0;
  min-height: 0;
  padding: 0;
  align-self: start;
  overflow: hidden;
  text-align: left;
}
.camera-placeholder {
  display: grid;
  place-items: center;
  aspect-ratio: 16/9;
  width: 100%;
  background: #202020;
}
.camera-placeholder .avatar {
  margin: 0;
  flex: none;
}
.participant-group.has-screen .person-state {
  z-index: 1;
  top: 12px;
  right: 12px;
  background: #111c;
  padding: 7px;
  border-radius: 9px;
}
.screen-slot {
  aspect-ratio: 16/9 !important;
}
.screen-slot :deep(video) {
  object-fit: contain;
}
.participant-group.has-screen .person-caption {
  display: flex;
  align-items: center;
  height: 48px;
  min-height: 48px;
  padding: 0 18px;
}
.participant-group.has-screen .person-caption strong {
  min-width: 0;
  max-width: 100%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  line-height: 18px;
}
.screen-caption {
  height: 48px;
}
.expand-screen {
  opacity: 1;
  padding: 8px;
  min-height: 32px;
  border-radius: 8px;
}
.expand-screen span {
  display: none;
}
@media (max-width: 700px) {
  .participant-group.has-screen {
    gap: 6px;
    padding: 4px;
    border-radius: 18px;
  }
  .participant-group.has-screen .person-caption {
    padding: 0 8px;
    font-size: 11px;
  }
  .screen-caption {
    padding-left: 8px !important;
    font-size: 10px;
  }
  .screen-caption > svg {
    display: none;
  }
}

.screen-card {
  overflow: visible;
}
.screen-slot {
  border-radius: 18px 18px 0 0;
  overflow: hidden;
}
.screen-caption {
  padding: 8px 12px 8px 17px;
  min-height: 48px;
}
.screen-caption > span {
  min-width: 0;
}
.screen-viewer-footer {
  display: flex;
  align-items: center;
  gap: 12px;
  min-height: 32px;
  flex-shrink: 0;
}
.screen-viewer-footer p {
  font-size: 11px;
  color: var(--quiet);
  margin: 0;
}
.screen-viewer-footer .screen-audio-control {
  margin-left: auto;
}

.screen-viewer header h2 {
  flex: 1;
}
.screen-viewer-body {
  position: relative;
  display: flex;
  flex: 1;
  min-height: 0;
  min-width: 0;
}
.screen-viewer-body .enlarged-slot {
  flex: 1;
  min-width: 0;
}
.media-admin-actions {
  position: absolute;
  right: 10px;
  bottom: 40px;
  z-index: 4;
  display: flex;
  gap: 4px;
  padding: 4px;
  background: #202020e8;
  border-radius: 10px;
  opacity: 0;
  pointer-events: none;
  transition: opacity 100ms ease;
}
.media-admin-actions button {
  display: grid;
  place-items: center;
  width: 32px;
  height: 32px;
  padding: 0;
  border-radius: 8px;
  background: transparent;
  color: #ddd;
}
.media-admin-actions button:hover {
  background: #393939;
  color: #fff;
}
.person-card:hover .media-admin-actions,
.person-card:focus-within .media-admin-actions,
.screen-card:hover .media-admin-actions,
.screen-card:focus-within .media-admin-actions {
  opacity: 1;
  pointer-events: auto;
}
.screen-admin-actions {
  left: 10px;
  right: auto;
  bottom: 48px;
}
.person-card .media-admin-actions {
  bottom: 10px;
  flex-direction: row-reverse;
  padding: 0;
  background: transparent;
}
.person-card .participant-volume,
.person-card .media-admin-actions button {
  width: 30px;
  height: 30px;
  padding: 0;
  border-radius: 9px;
  background: #202020e8;
  color: #ddd;
}
.person-card .participant-volume:hover,
.person-card .media-admin-actions button:hover {
  background: #393939;
  color: #fff;
}
.person-card:is(.with-camera, .has-screen .person-card):has(.media-admin-actions button)
  .person-caption {
  padding-right: 48px;
}
.person-card:is(.with-camera, .has-screen .person-card):has(
    .media-admin-actions button:nth-child(2)
  )
  .person-caption {
  padding-right: 82px;
}
@media (hover: none) {
  .media-admin-actions {
    opacity: 1;
    pointer-events: auto;
  }
}

.stream-viewer {
  position: fixed;
  z-index: 35;
  left: 266px;
  right: 16px;
  top: 84px;
  bottom: 16px;
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 10px;
  background: #161616;
  border-radius: 16px;
  box-shadow: 0 12px 44px #0009;
  min-width: 0;
  overflow: hidden;
}
.stream-viewer header {
  display: flex;
  align-items: center;
  gap: 5px;
  flex: none;
}
.stream-viewer h2 {
  flex: 1;
  min-width: 0;
  white-space: nowrap;
  text-overflow: ellipsis;
  overflow: hidden;
  font-size: 12px;
  font-weight: 500;
  margin: 0;
  padding-left: 5px;
}
.stream-viewer header button {
  width: 30px;
  height: 30px;
  padding: 6px;
  flex: none;
  background: #242424;
  border-radius: 9px;
}
.stream-viewer-body {
  display: flex;
  flex: 1;
  min-height: 0;
}
.stream-viewer .enlarged-slot {
  width: 100%;
  border-radius: 9px;
  overflow: hidden;
}
.stream-viewer :deep(video) {
  object-fit: contain;
}
.stream-mini {
  z-index: 8;
  left: auto;
  right: 24px;
  top: 88px;
  bottom: auto;
  width: min(320px, calc(100vw - 32px));
  padding: 8px;
  border-radius: 14px;
}
.stream-mini .drag-title,
.stream-mini .stream-viewer-body {
  cursor: grab;
  user-select: none;
  touch-action: none;
}
.stream-mini .stream-viewer-body :deep(video) {
  pointer-events: none;
}
.stream-mini .mini-resize {
  position: absolute;
  right: 2px;
  bottom: 2px;
  width: 22px;
  height: 22px;
  padding: 0;
  background: #181818d9;
  color: #ddd;
  display: grid;
  place-items: center;
  cursor: nwse-resize;
  touch-action: none;
}
.stream-mini .stream-viewer-body {
  flex: 1;
  aspect-ratio: 16/9;
  width: 100%;
}
.stream-mini .enlarged-slot {
  height: 100%;
}
:global(body:has(.app-shell.with-members) .stream-mini) {
  right: 230px;
}
@media (min-width: 801px) and (max-width: 1200px) {
  .stream-viewer:not(.stream-mini) {
    left: 236px;
  }
}
@media (max-width: 800px) {
  .stream-viewer:not(.stream-mini) {
    left: 16px;
    top: 170px;
    bottom: 16px;
  }
  .stream-mini {
    top: 170px;
    width: min(280px, calc(100vw - 32px));
  }
  :global(body:has(.app-shell.with-members) .stream-mini) {
    right: 16px;
  }
}
</style>
