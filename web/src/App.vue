<script setup lang="ts">
import {
  computed,
  defineAsyncComponent,
  onMounted,
  onBeforeUnmount,
  reactive,
  ref,
  watch,
  nextTick,
} from 'vue';
import {
  activeServer,
  clientOrigin,
  apiFetch,
  serverFetch,
  prepareServer,
  selectServerOrigin,
  restoreServerOrigin,
} from './composables/serverConnection';
import EnrollmentPage from './components/EnrollmentPage.vue';
import ServerSwitcher from './components/ServerSwitcher.vue';
import FileSettings from './components/FileSettings.vue';
import AntivirusSettings from './components/AntivirusSettings.vue';
import StorageSettings from './components/StorageSettings.vue';
import HotkeySettings from './components/HotkeySettings.vue';
import { useHotkeys, type Bindings } from './composables/useHotkeys';
import RoleEditor from './components/RoleEditor.vue';
import InvitationSettings from './components/InvitationSettings.vue';
import EffectPicker from './components/EffectPicker.vue';
import QuickEffects from './components/QuickEffects.vue';
import DockNotice from './components/DockNotice.vue';
import { roleLabel, editableMember } from './composables/roles';
import MemberMenu from './components/MemberMenu.vue';
import PasswordInput from './components/PasswordInput.vue';
import AppIcon from './components/AppIcon.vue';
import { firstSidebarChannel } from './composables/firstChannel';
import ChatPanel from './components/ChatPanel.vue';
import ChatNotificationToggle from './components/ChatNotificationToggle.vue';
import { useMentionAlerts } from './composables/useMentionAlerts';
import type { Mention } from './composables/mentions';
import WorkspaceSettings from './components/WorkspaceSettings.vue';
import CallStage from './components/CallStage.vue';
import AppSelect from './components/AppSelect.vue';
const AudioLabPanel =
  import.meta.env.VITE_AUDIO_LAB !== '0'
    ? defineAsyncComponent(() => import('./components/AudioLabPanel.vue'))
    : null;
import { downloadAudioDiagnostics } from './composables/audioDiagnostics';
import AudioTests from './components/AudioTests.vue';
import ProfilePanel from './components/ProfilePanel.vue';
import SessionRecovery from './components/SessionRecovery.vue';
import {
  readProfile,
  blankProfile,
  parseProfile,
  type ProfileFile,
  type SavedServer,
} from './composables/profileFile';
import { useProfileLibrary } from './composables/profileLibrary';
import {
  downloadEncryptedProfile,
  decryptProfile,
  inspectEncryptedProfile,
} from './composables/encryptedProfile';
import { useAvatars } from './composables/useAvatars';
import { useAudioDevices } from './composables/useAudioDevices';
import { useCall } from './composables/useCall';
import { useAccess } from './composables/useAccess';
import { useWorkspace, type Channel } from './composables/useWorkspace';

// Call/media hosts are owned by the application, never by a routed page.
const videos = ref<HTMLElement | null>(null),
  audio = ref<HTMLElement | null>(null);
const devices = useAudioDevices();
const { inputs, outputs, inputId, outputId, cameras, cameraId, busy, note, supportsOutput } =
  devices;
const call = useCall(devices, () => ({ videos: videos.value, audio: audio.value }));
const {
  joining,
  connected,
  hasRoom,
  status,
  mic,
  camera,
  screen,
  needsAudio,
  pending,
  participants,
  connection,
  supportsScreen,
  screenUnavailable,
  sendQuality,
  qualityNote,
  videoCount,
  videoTiles,
} = call;
const access = useAccess();
const {
  authenticated,
  authenticating,
  restoring,
  restoreFailed,
  error: accessError,
  profileKey,
  storageNote,
} = access;
const workspace = useWorkspace(access.session, async () => {
  const ok = await access.renew();
  if (access.restoreFailed.value)
    throw new Error('Сервер не отвечает. Пробуем восстановить соединение…');
  return ok;
});
const {
  name: spaceName,
  channels: rooms,
  members,
  self,
  ownerExists,
  presence,
  presenceAvailable,
  error: workspaceError,
  messages,
  sending,
} = workspace;
const chatAlerts = useMentionAlerts(
  workspace.notifications,
  workspace.selfId,
  workspace.ready,
  workspace.chatActivity,
);
const serverAvatars = useAvatars(access.session, members);
const avatars = computed(() => ({
  ...serverAvatars.value,
  ...(workspace.selfId.value && profileAvatar.value
    ? { [workspace.selfId.value]: profileAvatar.value }
    : {}),
}));
const networkIndicator = computed(() => {
  if (workspace.networkError.value) return { color: 'red', label: 'Сервер не отвечает' };
  if (call.linkQuality.value === 'lost') return { color: 'red', label: 'Потеря связи в звонке' };
  if (call.linkQuality.value === 'poor')
    return { color: 'yellow', label: 'Нестабильная связь в звонке' };
  const ms = workspace.latency.value;
  if (ms !== undefined && ms > 700)
    return { color: ms > 2000 ? 'red' : 'yellow', label: ms + ' мс' };
  return workspace.ready.value
    ? { color: 'green', label: 'Соединение в порядке' }
    : { color: 'yellow', label: 'Проверяем соединение' };
});
const profileLibrary = useProfileLibrary();
const localProfileOpen = ref(false);
const personal = ref(profileLibrary.current() || readProfile()),
  importPreview = ref<ProfileFile>(),
  transferNote = ref('');
if (!location.hash.startsWith('#/connect/')) restoreServerOrigin(personal.value.servers);
const initialEntry = personal.value.servers.find((s) => s.url === activeServer.value);
if (initialEntry) profileKey.value = initialEntry.profileKey;
const switchingServer = ref(''),
  switchError = ref('');
let serverSwitchAbort: AbortController | undefined;
const encryptedImport = ref(''),
  importPassword = ref(''),
  decrypting = ref(false);
let importGeneration = 0;
function cancelImport() {
  ++importGeneration;
  encryptedImport.value = '';
  importPassword.value = '';
  decrypting.value = false;
}
async function unlockImport() {
  if (decrypting.value) return;
  const run = importGeneration;
  decrypting.value = true;
  transferNote.value = '';
  try {
    const profile = await decryptProfile(encryptedImport.value, importPassword.value);
    if (run !== importGeneration) return;
    importPreview.value = profile;
    cancelImport();
    await acceptImport();
  } catch (e) {
    if (run === importGeneration) transferNote.value = (e as Error).message;
  } finally {
    if (run === importGeneration) decrypting.value = false;
  }
}
call.effects.enabled.value = personal.value.cameraEffects?.enabled ?? false;
call.effects.strength.value = personal.value.cameraEffects?.strength ?? 12;
watch([call.effects.enabled, call.effects.strength], ([enabled, strength]) => {
  personal.value = { ...personal.value, cameraEffects: { enabled, strength } };
  try {
    profileLibrary.save(personal.value);
  } catch {
    transferNote.value = 'Не удалось сохранить настройки камеры в профиле.';
  }
});
const membersExpanded = ref(false);
try {
  membersExpanded.value = localStorage.getItem('nimbo-members-expanded') === 'true';
} catch {
  /* optional preference */
}
function toggleMembers() {
  membersExpanded.value = !membersExpanded.value;
  try {
    localStorage.setItem('nimbo-members-expanded', String(membersExpanded.value));
  } catch {
    /* optional preference */
  }
}
const profileAvatar = ref(personal.value.avatar);
const volumePerson = ref<{ identity: string; name: string; x: number; y: number } | null>(null);
const mediaModerationBusy = ref(false),
  mediaModerationError = ref('');
const menuMember = computed(() =>
  members.value.find((m) => m.id === volumePerson.value?.identity.split('.')[0]),
);
const menuVoice = computed(
  () =>
    !!volumePerson.value &&
    Object.values(workspace.presence.value).some((room) =>
      room.people.some(
        (p) => p.identity.split('.')[0] === volumePerson.value!.identity.split('.')[0],
      ),
    ),
);
async function moderatePerson(identity: string, action: string, seconds?: number) {
  await workspace.mutate('/api/members/' + identity.split('.')[0] + '/moderate', 'POST', {
    action,
    seconds,
  });
}
async function moderateMedia(person: { identity: string }, action: string) {
  if (mediaModerationBusy.value) return;
  mediaModerationBusy.value = true;
  mediaModerationError.value = '';
  try {
    await moderatePerson(person.identity, action);
  } catch (e) {
    mediaModerationError.value = (e as Error).message;
  } finally {
    mediaModerationBusy.value = false;
  }
}
const inputOptions = computed(() =>
  inputs.value.map((d) => ({ value: d.deviceId, label: d.label })),
);
const outputOptions = computed(() =>
  outputs.value.map((d) => ({ value: d.deviceId, label: d.label })),
);
const cameraOptions = computed(() =>
  cameras.value.map((d) => ({ value: d.deviceId, label: d.label })),
);
const form = reactive({ name: '', password: '', withMic: true });
const firstChannel = computed(() =>
  firstSidebarChannel(rooms.value, workspace.sectionOrder.value, workspace.sections.value),
);
const selectedId = ref('');
const selectedChannel = computed(() => rooms.value.find((r) => r.id === selectedId.value));
const voiceRooms = computed(() => rooms.value.filter((r) => r.kind === 'voice'));
const textRooms = computed(() => rooms.value.filter((r) => r.kind === 'text'));
const isText = computed(() => selectedChannel.value?.kind === 'text');
type VoiceLocation = {
  origin: string;
  room: string;
  title: string;
  server: string;
  member: string;
  session: string;
};
const voice = ref<VoiceLocation>();
const localVoiceRoom = computed(() =>
  voice.value?.origin === activeServer.value ? voice.value.room : '',
);
const foreignVoice = computed(() => !!voice.value && voice.value.origin !== activeServer.value);
const viewingCall = computed(
  () => !!localVoiceRoom.value && selectedId.value === localVoiceRoom.value,
);
const compactDock = computed(() => page.value === '/room' && (isText.value || foreignVoice.value));
const roomTitle = computed(() => selectedChannel.value?.name || 'Пространство');
const invite = ref<HTMLDialogElement | null>(null),
  copied = ref('');
const currentPath = ref(window.location.hash.slice(1) || '/profile');
const routes = [
  '/room',
  '/profile',
  '/settings/audio',
  '/settings/video',
  '/settings/general',
  '/settings/workspace',
  '/settings/channels',
  '/settings/roles',
  '/settings/hotkeys',
  '/settings/storage',
];
if (currentPath.value === '/settings/profile') currentPath.value = '/profile';
const page = computed(() =>
  !authenticated.value
    ? '/login'
    : routes.includes(currentPath.value)
      ? currentPath.value === '/settings/storage'
        ? '/settings/general'
        : currentPath.value
      : '/profile',
);
const settingsOpen = computed(() => page.value.startsWith('/settings/'));
const section = computed(() =>
  page.value === '/settings/roles' ? 'workspace' : page.value.split('/').at(-1),
);
const sections = [
  { id: 'general', name: 'Общие', icon: 'settings' },
  { id: 'audio', name: 'Звук', icon: 'headphones' },
  { id: 'video', name: 'Видео', icon: 'camera' },
  { id: 'channels', name: 'Каналы', icon: 'hash' },
  { id: 'workspace', name: 'Участники и роли', icon: 'users' },
  { id: 'hotkeys', name: 'Горячие клавиши', icon: 'keyboard' },
];
const hotkeys = useHotkeys(workspace.selfId, (id) => {
  if (!authenticated.value) return;
  if (id === 'members') {
    navigate('/room');
    toggleMembers();
    return;
  }
  if (id === 'settings') {
    navigate('/settings/audio');
    return;
  }
  if (!connected.value || pending.value || joining.value) return;
  if (id === 'effects') {
    if (camera.value && !call.effects.busy.value) void call.effects.toggle();
    return;
  }
  if (id === 'screen' && !supportsScreen) return;
  void call.toggle(id);
});
let importedHotkeys: Bindings | undefined;
try {
  if (sessionStorage.getItem('nimbo-import-hotkeys') === 'pending')
    importedHotkeys = personal.value.hotkeys;
} catch {}
watch(
  [workspace.selfId, workspace.ready],
  ([id, ready]) => {
    if (!id || !ready) return;
    let missing = false;
    try {
      missing = localStorage.getItem('nimbo-hotkeys:' + id) === null;
    } catch {}
    const portable = importedHotkeys ?? (missing ? personal.value.hotkeys : undefined);
    if (portable) hotkeys.replace(portable);
    importedHotkeys = undefined;
    try {
      sessionStorage.removeItem('nimbo-import-hotkeys');
    } catch {}
  },
  { flush: 'post' },
);
const locked = computed(() => hasRoom.value || joining.value);
const qualityBusy = computed(
  () => joining.value || !!pending.value || (hasRoom.value && !connected.value),
);
const invitationUrl = computed(
  () => activeServer.value + '/?room=' + encodeURIComponent(selectedId.value),
);
const configController = new AbortController();
const drafts = reactive<Record<string, string>>({}),
  chatList = ref<HTMLElement | null>(null);
const serverFileDrafts = reactive<Record<string, Record<string, File[]>>>({});
const activeFileDrafts = computed(
  () => serverFileDrafts[activeServer.value] ?? (serverFileDrafts[activeServer.value] = {}),
);
const draft = computed({
  get: () => drafts[activeServer.value + '|' + selectedId.value] || '',
  set: (value: string) => (drafts[activeServer.value + '|' + selectedId.value] = value),
});
function initials(name: string) {
  return name.trim().slice(0, 2).toUpperCase() || 'Н';
}
function navigate(path: string) {
  currentPath.value = path;
  if (window.location.hash !== '#' + path) window.location.hash = path;
}
function openHome() {
  const first = firstChannel.value;
  navigate('/room');
  if (!first) return;
  selectedId.value = first.id;
  void workspace.selectText(first.kind === 'text' ? first.id : '');
}
const enrollmentPage = location.hash.startsWith('#/enroll/');
let connectingProfile = false;
function readPath() {
  const path = window.location.hash.slice(1) || '/room';
  if (path.startsWith('/connect/')) {
    selectServerOrigin(clientOrigin);
    const key = path.slice(9);
    history.replaceState(null, '', window.location.pathname + window.location.search + '#/profile');
    currentPath.value = '/profile';
    if (/^[A-Za-z0-9_-]{43}$/.test(key)) {
      connectingProfile = true;
      void connectSavedServer(key);
    }
    return;
  }
  if (path.startsWith('/invite/')) {
    let code = '';
    try {
      code = decodeURIComponent(path.slice(8));
    } catch {}
    if (code.length >= 16 && code.length <= 256 && !/[\x00-\x1f]/.test(code)) {
      form.password = code;
      localProfileOpen.value = false;
      history.replaceState(
        null,
        '',
        window.location.pathname + window.location.search + '#/profile',
      );
      currentPath.value = '/profile';
      return;
    }
  }
  currentPath.value = path;
}
let serverNamesBusy = false;
async function refreshServerNames() {
  if (serverNamesBusy) return;
  serverNamesBusy = true;
  const profileId = profileLibrary.activeId.value;
  try {
    const names = await Promise.all(
      personal.value.servers
        .filter((s) => !s.customName)
        .map(async (server) => {
          try {
            const response = await serverFetch(server.url, '/api/config', {
              signal: AbortSignal.timeout(5000),
            });
            if (!response.ok) return;
            const data = await response.json();
            if (
              typeof data.serverName === 'string' &&
              data.serverName.trim() &&
              [...data.serverName].length <= 60 &&
              !/[\u0000-\u001f\u007f]/.test(data.serverName)
            )
              return { url: server.url, name: data.serverName };
          } catch {}
        }),
    );
    if (profileId !== profileLibrary.activeId.value) return;
    const servers = personal.value.servers.map((server) => {
      const next = names.find((n) => n?.url === server.url);
      return !server.customName && next ? { ...server, name: next.name } : server;
    });
    if (servers.some((s, i) => s.name !== personal.value.servers[i]?.name)) saveServers(servers);
  } finally {
    serverNamesBusy = false;
  }
}
watch(
  () => page.value,
  (value) => {
    if (value === '/profile') void refreshServerNames();
  },
);
async function switchServer(server: SavedServer) {
  if (switchingServer.value) return;
  if (server.url === activeServer.value && authenticated.value) {
    openHome();
    return;
  }
  if (workspace.sending.value || workspace.pendingMessages.value.length) {
    switchError.value =
      'Есть неотправленные вложения. Дождитесь отправки или удалите их перед переключением сервера.';
    return;
  }
  switchingServer.value = server.name;
  switchError.value = '';
  serverSwitchAbort = new AbortController();
  try {
    const prepared = await prepareServer(
      server,
      AbortSignal.any([serverSwitchAbort.signal, AbortSignal.timeout(15000)]),
    );
    if (authenticated.value) {
      personal.value.hotkeys = { ...hotkeys.bindings.value };
      rememberProfile();
    }
    if (voice.value && localVoiceRoom.value && voice.value.member === workspace.selfId.value) {
      voice.value.session = access.session.value;
      voice.value.server = spaceName.value;
      voice.value.title =
        rooms.value.find((room) => room.id === localVoiceRoom.value)?.name || voice.value.title;
    }
    workspace.stop();
    configController.abort();
    selectServerOrigin(server.url);
    access.reset();
    try {
      localStorage.removeItem('nimbo-signed-out:' + server.url);
    } catch {}
    profileKey.value = server.profileKey;
    access.session.value = prepared.access.session;
    access.member.value = prepared.access.member;
    authenticated.value = true;
    form.name = prepared.access.member.name;
    form.password = '';
    selectedId.value = '';
    volumePerson.value = null;
    invite.value?.close();
    profileAvatar.value = prepared.access.member.avatar || personal.value.avatar;
    localProfileOpen.value = false;
    workspace.start(prepared.workspace);
    navigate('/room');
    await nextTick();
    rememberProfile();
    void syncProfileAvatar();
  } catch (e) {
    switchError.value =
      e instanceof Error && e.name !== 'TypeError'
        ? e.message
        : 'Не удалось подключиться. Проверьте доступность и версию сервера.';
  } finally {
    switchingServer.value = '';
    serverSwitchAbort = undefined;
  }
}
async function syncProfileAvatar() {
  if (!authenticated.value || access.member.value?.avatar || !profileAvatar.value) return;
  const avatar = profileAvatar.value,
    token = access.session.value;
  try {
    await workspace.mutate('/api/profile', 'PATCH', { name: form.name, avatar });
    if (access.session.value === token && access.member.value) access.member.value.avatar = avatar;
  } catch {
    if (access.session.value === token)
      transferNote.value =
        'Аватар пока не удалось сохранить на сервере. Нажмите «Сохранить профиль», чтобы повторить.';
  }
}
async function connectSavedServer(key: string) {
  if (authenticated.value && profileKey.value === key) {
    connectingProfile = false;
    navigate('/profile');
    return;
  }
  const previousKey = profileKey.value;
  try {
    const existing = profileLibrary.entries.value.find((p) =>
      p.profile.servers.some((s) => s.url === activeServer.value && s.profileKey === key),
    );
    if (!existing && profileLibrary.entries.value.length >= 16)
      throw new Error('Можно хранить до 16 профилей. Сначала удалите ненужный.');
    profileKey.value = key;
    if (!(await access.signIn('', '', true))) {
      profileKey.value = previousKey;
      throw new Error(access.error.value || 'Не удалось открыть сервер.');
    }
    clearResume();
    await call.leave();
    workspace.stop();
    const profile = existing?.profile || {
      ...blankProfile(),
      name: access.member.value!.name,
      avatar: access.member.value?.avatar || '',
      servers: [
        {
          url: activeServer.value,
          name: new URL(activeServer.value).host,
          displayName: access.member.value!.name,
          profileKey: key,
        },
      ],
    };
    const id = existing?.id || profileLibrary.add(profile);
    profileLibrary.select(id);
    applyPersonal(profile);
    finishSignIn(true);
    navigate('/profile');
  } catch (e) {
    transferNote.value = (e as Error).message;
  } finally {
    connectingProfile = false;
  }
}
async function signIn() {
  if (!(await access.signIn(form.name, form.password))) return;
  finishSignIn(true);
}
function finishSignIn(addServer = false) {
  selectedId.value = '';
  localProfileOpen.value = false;
  transferNote.value = '';
  cancelImport();
  importPreview.value = undefined;
  form.password = '';
  form.name = access.member.value?.name || form.name.trim();
  status.value = 'Выберите комнату и подключитесь, когда будете готовы.';
  profileAvatar.value = access.member.value?.avatar || personal.value.avatar;
  rememberProfile(addServer || !personal.value.name);
  try {
    localStorage.setItem('nimbo-name', form.name);
  } catch {
    /* optional preference */
  }
  workspace.start();
  void syncProfileAvatar();
  navigate(routes.includes(currentPath.value) ? currentPath.value : '/room');
}
async function signOut() {
  clearResume();
  reportVoice('left');
  if (!(await access.signOut())) {
    transferNote.value = access.error.value;
    return false;
  }
  workspace.stop();
  form.password = '';
  invite.value?.close();
  navigate('/login');
  for (const key of Object.keys(serverFileDrafts)) delete serverFileDrafts[key];
  for (const key of Object.keys(drafts)) delete drafts[key];
  await call.leave();
  return true;
}
watch(workspace.expired, (value) => {
  if (value && authenticated.value) {
    // Only an explicit sign-out revokes the remembered session on the server.
    clearResume();
    workspace.stop();
    access.reset();
    void call.leave();
    navigate('/login');
    transferNote.value =
      'Сохранённый вход больше не действует. Войдите заново или обратитесь к владельцу.';
  }
});
function rememberProfile(addServer = false) {
  const previous = personal.value.servers.find((s) => s.url === activeServer.value);
  const entry = {
    ...previous,
    url: activeServer.value,
    name: previous?.customName ? previous.name : spaceName.value,
    displayName: form.name,
    profileKey: profileKey.value,
    ...(self.value?.role ? { role: self.value.role } : {}),
  };
  const servers = previous
    ? personal.value.servers.map((s) => (s.url === entry.url ? entry : s))
    : addServer
      ? [...personal.value.servers, entry]
      : personal.value.servers;
  personal.value = {
    ...personal.value,
    format: 'nimbo-profile',
    version: 1,
    name: form.name,
    avatar: profileAvatar.value,
    servers,
  };
  try {
    profileLibrary.save(personal.value);
    localStorage.setItem('nimbo-name', form.name);
  } catch {
    transferNote.value = 'Профиль не сохранился в браузере. Скачайте его файл.';
  }
}
watch([spaceName, () => self.value?.role], () => {
  if (authenticated.value && !switchingServer.value) rememberProfile();
});
async function leaveServer(url: string) {
  if (url !== activeServer.value) throw new Error('Откройте этот сервер, чтобы покинуть его.');
  if (!authenticated.value) throw new Error('Войдите на сервер, чтобы удалить своё участие.');
  if (self.value?.role === 'owner')
    throw new Error('Сначала передайте владение сервером другому участнику.');
  const response = await apiFetch('/api/membership', {
    method: 'DELETE',
    headers: { Authorization: 'Bearer ' + access.session.value },
    signal: AbortSignal.timeout(15000),
  });
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || 'Не удалось покинуть сервер.');
  clearResume();
  access.reset();
  workspace.stop();
  await call.leave();
  for (const key of Object.keys(serverFileDrafts)) delete serverFileDrafts[key];
  for (const key of Object.keys(drafts)) delete drafts[key];
  profileKey.value = '';
  try {
    localStorage.removeItem('nimbo-profile-key');
  } catch {}
  saveServers(personal.value.servers.filter((s) => s.url !== url));
  localProfileOpen.value = true;
  navigate('/profile');
  transferNote.value = 'Вы покинули сервер. Ваш профиль удалён из списка участников.';
}
function saveServers(servers: ProfileFile['servers']) {
  const next = { ...personal.value, servers };
  profileLibrary.save(next);
  personal.value = next;
}
function addLocalProfile(profile: ProfileFile) {
  const id = profileLibrary.add(profile);
  if (id === profileLibrary.activeId.value) applyPersonal(profileLibrary.current()!);
  return id;
}
async function saveProfile(name: string, avatar: string) {
  await workspace.mutate('/api/profile', 'PATCH', { name, avatar });
  form.name = name;
  profileAvatar.value = avatar;
  rememberProfile();
}
async function exportProfile(password: string) {
  if (authenticated.value) {
    personal.value.hotkeys = { ...hotkeys.bindings.value };
    rememberProfile();
  }
  await downloadEncryptedProfile(personal.value, password);
}
function applyPersonal(profile: ProfileFile) {
  personal.value = profile;
  call.effects.enabled.value = profile.cameraEffects?.enabled ?? false;
  call.effects.strength.value = profile.cameraEffects?.strength ?? 12;
  const entry = profile.servers.find((s) => s.url === activeServer.value);
  form.name = entry?.displayName || profile.name;
  profileKey.value = entry?.profileKey || '';
  profileAvatar.value = profile.avatar;
  importedHotkeys = profile.hotkeys;
  try {
    if (profile.hotkeys) sessionStorage.setItem('nimbo-import-hotkeys', 'pending');
    else sessionStorage.removeItem('nimbo-import-hotkeys');
    localStorage.setItem('nimbo-profile-key', profileKey.value);
    localStorage.setItem('nimbo-name', form.name);
  } catch {
    transferNote.value = 'Браузер не сохранил выбранный профиль.';
  }
}
async function switchLocalProfile(id: string, code: string) {
  const target = profileLibrary.entries.value.find((p) => p.id === id);
  if (!target || id === profileLibrary.activeId.value) return;
  if (authenticated.value) {
    personal.value.hotkeys = { ...hotkeys.bindings.value };
    rememberProfile();
  }
  const previousKey = profileKey.value,
    entry = target.profile.servers.find((s) => s.url === activeServer.value);
  profileKey.value = entry?.profileKey || '';
  if (!(await access.signIn(entry?.displayName || target.profile.name, code, !code))) {
    profileKey.value = previousKey;
    throw new Error(access.error.value || 'Не удалось переключить профиль.');
  }
  const nextKey = profileKey.value;
  clearResume();
  reportVoice('left');
  await call.leave();
  workspace.stop();
  for (const key of Object.keys(serverFileDrafts)) delete serverFileDrafts[key];
  for (const key of Object.keys(drafts)) delete drafts[key];
  profileLibrary.select(id);
  applyPersonal(target.profile);
  profileKey.value = nextKey;
  try {
    localStorage.setItem('nimbo-profile-key', nextKey);
  } catch {}
  finishSignIn(true);
  navigate('/profile');
}
async function removeLocalProfile(id: string) {
  if (id === profileLibrary.activeId.value) {
    if (authenticated.value && !(await signOut()))
      throw new Error(access.error.value || 'Не удалось выйти из профиля.');
    localProfileOpen.value = false;
    profileLibrary.remove(id);
    applyPersonal(blankProfile());
  } else profileLibrary.remove(id);
}
async function loadProfile(e: Event) {
  const input = e.target as HTMLInputElement,
    file = input.files?.[0];
  input.value = '';
  if (!file) return;
  cancelImport();
  const run = importGeneration;
  importPreview.value = undefined;
  transferNote.value = '';
  try {
    if (file.size > 300000) throw new Error('Файл слишком большой.');
    const text = await file.text();
    if (run !== importGeneration) return;
    if (JSON.parse(text)?.format === 'nimbo-encrypted-profile') {
      inspectEncryptedProfile(text);
      encryptedImport.value = text;
    } else {
      importPreview.value = parseProfile(text);
      await acceptImport();
    }
  } catch (e) {
    if (run === importGeneration) transferNote.value = (e as Error).message;
  }
}
async function loginSavedProfile() {
  const entry = personal.value.servers.find((s) => s.url === activeServer.value);
  if (entry) {
    await connectSavedServer(entry.profileKey);
    return;
  }
  localProfileOpen.value = false;
  transferNote.value = 'Для первого подключения нужен код приглашения.';
}
async function saveLocalProfile(name: string, avatar: string) {
  const next = parseProfile(JSON.stringify({ ...personal.value, name, avatar }));
  profileLibrary.save(next);
  applyPersonal(next);
}
function selectLoginProfile(event: Event) {
  const id = (event.target as HTMLSelectElement).value;
  try {
    profileLibrary.select(id);
    applyPersonal(profileLibrary.current()!);
  } catch (e) {
    transferNote.value = (e as Error).message;
  }
}
async function acceptImport() {
  const imported = importPreview.value;
  if (!imported) return;
  try {
    const id = profileLibrary.add(imported);
    profileLibrary.select(id);
    applyPersonal(profileLibrary.current()!);
    importPreview.value = undefined;
    if (profileKey.value && (await access.signIn(form.name, '', true))) {
      finishSignIn(true);
      navigate('/profile');
    } else {
      const reason = access.error.value;
      access.error.value = '';
      localProfileOpen.value = true;
      navigate('/profile');
      transferNote.value =
        reason ||
        'Это локальный профиль. Для первого подключения к этому серверу потребуется код приглашения.';
    }
  } catch (e) {
    transferNote.value = (e as Error).message;
  }
}
function openVolume(
  person: { identity: string; name: string; local?: boolean },
  event?: MouseEvent,
) {
  if (person.local || person.identity.split('.')[0] === self.value?.id) return;
  const rect = (event?.currentTarget as HTMLElement)?.getBoundingClientRect?.();
  volumePerson.value = {
    ...person,
    x: event?.clientX || rect?.left || innerWidth / 2 - 126,
    y: event?.clientY || rect?.bottom || 100,
  };
}
watch(
  () => page.value,
  () => (volumePerson.value = null),
);
function isOwnProfile(person: { identity: string; local?: boolean }) {
  return !!person.local || person.identity.split('.')[0] === workspace.selfId.value;
}
function avatarFor(person: { identity: string; local?: boolean }) {
  const id = person.identity.split('.')[0]!;
  return isOwnProfile(person) ? profileAvatar.value || avatars.value[id] : avatars.value[id];
}
function displayPerson(person: {
  identity: string;
  name: string;
  local: boolean;
  speaking: boolean;
  mic: boolean;
  reconnecting?: boolean;
}) {
  const member = members.value.find((m) => m.id === person.identity.split('.')[0]);
  return {
    ...person,
    ownProfile: isOwnProfile(person),
    name: member?.name || person.name,
    avatar: avatarFor(person),
    canModerateMedia:
      !!member && editableMember(self.value, member) && workspace.can('kick_members'),
  };
}
const stagePeople = computed(() =>
  localVoiceRoom.value ? peopleIn(localVoiceRoom.value).map(displayPerson) : [],
);
const waitingPeople = computed(() =>
  peopleIn(selectedId.value)
    .filter((p) => !p.local)
    .map(displayPerson),
);
type ResumeRoom = {
  origin?: string;
  room: string;
  identity: string;
  member: string;
  mic: boolean;
  at: number;
};
let resumeRoom: ResumeRoom | undefined,
  pageLeaving = false;
try {
  const saved = JSON.parse(sessionStorage.getItem('nimbo-resume-room') || 'null');
  if (
    saved &&
    typeof saved.room === 'string' &&
    typeof saved.identity === 'string' &&
    typeof saved.member === 'string' &&
    typeof saved.mic === 'boolean' &&
    Date.now() - saved.at < 12 * 60 * 60 * 1000 &&
    Date.now() >= saved.at
  )
    resumeRoom = saved;
} catch {
  /* optional recovery */
}
function clearResume() {
  resumeRoom = undefined;
  try {
    sessionStorage.removeItem('nimbo-resume-room');
  } catch {}
}
function rememberRoom() {
  const location = voice.value;
  const person = participants.value.find((p) => p.local);
  if (!connected.value || !person || !location) return;
  const saved: ResumeRoom = {
    origin: location.origin,
    room: location.room,
    identity: person.identity,
    member: location.member,
    mic: mic.value,
    at: Date.now(),
  };
  try {
    sessionStorage.setItem('nimbo-resume-room', JSON.stringify(saved));
  } catch {}
}
watch([connected, mic, () => participants.value.find((p) => p.local)?.identity], () => {
  if (!pageLeaving) rememberRoom();
});
watch(hasRoom, (present, previous) => {
  if (previous && !present && !pageLeaving && !changingRoom) {
    clearResume();
    voice.value = undefined;
  }
});
function reportVoice(
  state: 'reconnecting' | 'left',
  identity = participants.value.find((p) => p.local)?.identity,
  location = voice.value,
) {
  if (!identity || !location) return;
  const session =
    location.origin === activeServer.value && location.member === workspace.selfId.value
      ? access.session.value
      : location.session;
  void serverFetch(location.origin, '/api/voice-state', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Authorization: 'Bearer ' + session },
    body: JSON.stringify({ identity, room: location.room, state }),
    keepalive: true,
  }).catch(() => {});
}
watch(workspace.ready, async (ready) => {
  if (!ready || !resumeRoom) return;
  const saved = resumeRoom;
  resumeRoom = undefined;
  if (
    (saved.origin && saved.origin !== activeServer.value) ||
    saved.member !== workspace.selfId.value ||
    !rooms.value.some((r) => r.id === saved.room && r.kind === 'voice')
  ) {
    clearResume();
    return;
  }
  form.withMic = saved.mic;
  await chooseRoom(saved.room);
  form.withMic = true;
  if (!hasRoom.value) clearResume();
  if (voice.value?.room === saved.room) reportVoice('left', saved.identity, voice.value);
});
let changingRoom = false;
const pendingRoom = ref('');
function leaveRoom() {
  clearResume();
  reportVoice('left');
  pendingRoom.value = '';
  voice.value = undefined;
  void call.leave();
}
async function returnToCall() {
  const original = voice.value;
  const server = personal.value.servers.find((s) => s.url === original?.origin);
  if (!original || !server) return;
  await switchServer(server);
  if (voice.value === original && locked.value && localVoiceRoom.value === original.room) {
    selectedId.value = original.room;
    navigate('/room');
    void workspace.selectText('');
  }
}
async function chooseRoom(id: string) {
  const ch = rooms.value.find((r) => r.id === id);
  if (!ch) return;
  selectedId.value = id;
  navigate('/room');
  if (ch.kind === 'text') {
    void workspace.selectText(id);
    return;
  }
  void workspace.selectText('');
  if (joining.value || changingRoom || (id === localVoiceRoom.value && hasRoom.value)) return;
  clearResume();
  changingRoom = true;
  pendingRoom.value = id;
  const target = {
    origin: activeServer.value,
    room: id,
    title: ch.name,
    server: spaceName.value,
    member: workspace.selfId.value,
    session: access.session.value,
  };
  try {
    if (hasRoom.value) {
      reportVoice('left');
      await call.leave();
    }
    if (!authenticated.value || activeServer.value !== target.origin) return;
    voice.value = target;
    await call.join({ ...form, room: id, session: target.session });
  } finally {
    changingRoom = false;
    pendingRoom.value = '';
    if (!hasRoom.value) voice.value = undefined;
  }
}
function peopleIn(id: string) {
  if (!pendingRoom.value && connected.value && id === localVoiceRoom.value)
    return participants.value.map((p) => ({ ...p, pending: false, reconnecting: false }));
  const others = (presenceAvailable.value ? presence.value[id]?.people || [] : [])
    .filter((p) => !call.isOwnConnection(activeServer.value, p.identity))
    .map((p) => ({
      ...p,
      local: false,
      speaking: false,
      mic: false,
      micKnown: false,
      camera: false,
      screen: false,
      pending: false,
    }));
  if (localVoiceRoom.value === id && pendingRoom.value === id)
    others.push({
      identity: (self.value?.id || 'local') + '.pending',
      name: form.name,
      joinedAt: Date.now(),
      local: true,
      speaking: false,
      mic: false,
      micKnown: false,
      camera: false,
      screen: false,
      pending: true,
    });
  return others;
}
watch(rooms, () => {
  if (!workspace.ready.value) return;
  if (
    hasRoom.value &&
    localVoiceRoom.value &&
    !rooms.value.some((r) => r.id === localVoiceRoom.value)
  ) {
    clearResume();
    void call.leave('Комната удалена администратором.');
  }
  if (!rooms.value.some((r) => r.id === selectedId.value)) {
    const requested = new URLSearchParams(window.location.search).get('room');
    selectedId.value =
      rooms.value.find((r) => r.id === requested)?.id || firstChannel.value?.id || '';
  }
  void workspace.selectText(
    rooms.value.find((r) => r.id === selectedId.value)?.kind === 'text' ? selectedId.value : '',
  );
});
watch(
  () => messages.value.at(-1)?.id,
  async () => {
    const el = chatList.value;
    if (!el) return;
    const nearBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 110;
    await nextTick();
    if (nearBottom) el.scrollTop = el.scrollHeight;
  },
);
watch(selectedId, async () => {
  await nextTick();
  if (chatList.value) chatList.value.scrollTop = chatList.value.scrollHeight;
});

async function sendMessage(replyTo = '', mentions: Mention[] = [], files?: File[]) {
  const id = activeServer.value + '|' + selectedId.value,
    text = draft.value;
  const accepted = await workspace.send(text, replyTo, mentions, files);
  if (accepted && drafts[id] === text) drafts[id] = '';
  return accepted;
}
function timeLabel(value: number) {
  return new Intl.DateTimeFormat('ru', {
    hour: '2-digit',
    minute: '2-digit',
    day: 'numeric',
    month: 'short',
  }).format(value);
}
async function copyInvite() {
  try {
    await navigator.clipboard.writeText(invitationUrl.value);
    copied.value = 'Ссылка скопирована';
  } catch {
    copied.value = 'Не удалось скопировать. Ссылку можно выделить вручную.';
  }
}
function openInvite() {
  copied.value = '';
  invite.value?.showModal();
}
function beforeUnload() {
  rememberRoom();
}
function pageHide() {
  rememberRoom();
  pageLeaving = true;
  reportVoice('left');
  void call.leave();
}
function pageShow(event: PageTransitionEvent) {
  if (event.persisted) window.location.reload();
}
let restoreTimer: ReturnType<typeof setTimeout> | undefined,
  restoreAttempts = 0;
async function restoreSignIn() {
  clearTimeout(restoreTimer);
  if (restoring.value || authenticated.value) return;
  const ok = await access.restore();
  if (ok) {
    restoreAttempts = 0;
    finishSignIn();
  } else if (restoreFailed.value) {
    restoreTimer = setTimeout(
      () => void restoreSignIn(),
      Math.min(15000, 3000 * ++restoreAttempts),
    );
  } else if (personal.value.name && !form.password) {
    localProfileOpen.value = true;
  }
}
function connectionOnline() {
  if (restoreFailed.value && !authenticated.value) void restoreSignIn();
}
onMounted(() => {
  if (enrollmentPage) return;
  readPath();
  try {
    form.name = localStorage.getItem('nimbo-name') || '';
  } catch {
    /* optional preference */
  }
  if (!connectingProfile) {
    if (profileLibrary.current()) applyPersonal(profileLibrary.current()!);
    void restoreSignIn();
  }
  window.addEventListener('online', connectionOnline);
  void devices.refresh();
  window.addEventListener('beforeunload', beforeUnload);
  window.addEventListener('pagehide', pageHide);
  window.addEventListener('pageshow', pageShow);
  window.addEventListener('hashchange', readPath);
  const timeout = setTimeout(() => configController.abort(), 10000);
  void apiFetch('/api/config', { signal: configController.signal })
    .then(async (response) => {
      if (!response.ok) throw new Error();
      const requested = new URLSearchParams(window.location.search).get('room');
      if (!selectedId.value && workspace.ready.value)
        selectedId.value =
          rooms.value.find((room) => room.id === requested)?.id || firstChannel.value?.id || '';
    })
    .catch(() => {
      status.value = 'Сервис пока недоступен. Попробуйте обновить страницу.';
    })
    .finally(() => clearTimeout(timeout));
});
onBeforeUnmount(() => {
  serverSwitchAbort?.abort();
  clearTimeout(restoreTimer);
  window.removeEventListener('online', connectionOnline);
  configController.abort();
  workspace.stop();
  access.reset();
  window.removeEventListener('beforeunload', beforeUnload);
  window.removeEventListener('pagehide', pageHide);
  window.removeEventListener('pageshow', pageShow);
  window.removeEventListener('hashchange', readPath);
  void call.leave();
  devices.dispose();
});
</script>

<template>
  <div
    v-if="switchingServer || switchError"
    class="server-switch-status"
    role="status"
    :aria-busy="!!switchingServer"
  >
    <span>{{ switchingServer ? 'Подключаемся: ' + switchingServer : switchError }}</span
    ><button v-if="switchingServer" class="text-button" @click="serverSwitchAbort?.abort()">
      Отмена</button
    ><button v-else class="text-button" @click="switchError = ''">Закрыть</button>
  </div>
  <EnrollmentPage v-if="enrollmentPage" />
  <SessionRecovery
    v-else-if="!authenticated && (restoring || restoreFailed)"
    :failed="restoreFailed"
    :busy="restoring"
    @retry="restoreSignIn"
  />
  <main v-else-if="!authenticated && localProfileOpen" class="local-profile-home">
    <header>
      <a class="brand" href="#/profile">nimbo.</a
      ><button class="text-button" @click="loginSavedProfile" :disabled="authenticating">
        Войти на этот сервер
      </button>
    </header>
    <h1>Профиль</h1>
    <ProfilePanel
      @open-server="switchServer"
      name-label="Имя профиля"
      :name="personal.name"
      :avatar="personal.avatar"
      :servers="personal.servers"
      :save-servers="saveServers"
      :leave-server="leaveServer"
      :profiles="profileLibrary.entries.value"
      :active-profile-id="profileLibrary.activeId.value"
      :add-profile="addLocalProfile"
      :switch-profile="switchLocalProfile"
      :remove-profile="removeLocalProfile"
      :save="saveLocalProfile"
      :export-file="exportProfile"
      @logout="localProfileOpen = false"
    />
    <p v-if="transferNote" class="field-help" role="status">{{ transferNote }}</p>
  </main>
  <main v-else-if="!authenticated" class="auth-page">
    <div class="auth-brand">
      <span class="brand">nimbo<span>.</span></span>
    </div>
    <section class="auth-form" aria-labelledby="login-title">
      <div class="modal-symbol"><AppIcon name="sound" :size="26" /></div>
      <h1 id="login-title">Войти в Nimbo</h1>
      <p class="modal-intro">
        Войдите через свой файл .nimbo. Для первого подключения к серверу нужны имя и код
        приглашения.
      </p>
      <form @submit.prevent="signIn">
        <label class="file-button restore-profile"
          >Войти через файл .nimbo<input
            type="file"
            accept=".nimbo,.json,application/json,application/octet-stream"
            :disabled="authenticating || decrypting"
            @change="loadProfile"
        /></label>
        <div v-if="encryptedImport" class="editor-fill">
          <strong>Защищённый профиль</strong
          ><label
            >Пароль файла<PasswordInput
              v-model="importPassword"
              autocomplete="off"
              maxlength="256"
              :disabled="decrypting"
              @keydown.enter.prevent="unlockImport" /></label
          ><button
            type="button"
            class="soft-button"
            :disabled="decrypting || !importPassword"
            @click="unlockImport"
          >
            {{ decrypting ? 'Расшифровываем…' : 'Открыть профиль' }}</button
          ><button type="button" class="text-button" :disabled="decrypting" @click="cancelImport">
            Отмена
          </button>
        </div>
        <div v-if="importPreview" class="editor-fill">
          <strong>{{ importPreview.name }}</strong>
          <p class="field-help">
            Серверов в файле: {{ importPreview.servers.length }}. Профиль сохранится в этом
            браузере; остальные профили останутся.
          </p>
          <button type="button" class="soft-button" @click="acceptImport">
            Войти с этим профилем</button
          ><button type="button" class="text-button" @click="importPreview = undefined">
            Отмена
          </button>
        </div>
        <p v-if="transferNote" class="field-help" role="status">{{ transferNote }}</p>
        <p v-if="accessError" class="form-error" role="alert">{{ accessError }}</p>
        <p class="auth-code-divider">или войдите по коду приглашения</p>
        <label v-if="profileLibrary.entries.value.length"
          >Сохранённый профиль<select
            :value="profileLibrary.activeId.value"
            :disabled="authenticating"
            @change="selectLoginProfile"
          >
            <option v-if="!profileLibrary.activeId.value" value="" disabled>
              Выберите профиль
            </option>
            <option v-for="entry in profileLibrary.entries.value" :key="entry.id" :value="entry.id">
              {{ entry.profile.name }}
            </option>
          </select></label
        >
        <label
          >Ваше имя<input
            v-model="form.name"
            :disabled="authenticating"
            required
            maxlength="40"
            autocomplete="nickname"
            placeholder="Как к вам обращаться?"
        /></label>
        <label
          >Код доступа<input
            v-model="form.password"
            :disabled="authenticating"
            type="password"
            required
            minlength="16"
            maxlength="256"
            autocomplete="current-password"
            placeholder="Код от организатора"
        /></label>
        <button type="submit" class="primary wide" :disabled="authenticating">
          {{ authenticating ? 'Проверяем код…' : 'Войти в Nimbo'
          }}<AppIcon name="arrow" :size="18" />
        </button>
      </form>
      <p class="field-help">
        При подключении к голосовой комнате включится микрофон. Камера включается отдельно.
      </p>
    </section>
  </main>

  <div
    v-show="authenticated"
    :inert="switchingServer ? true : undefined"
    class="app-shell"
    :class="{
      'with-members': page === '/room' && membersExpanded,
      'has-member-panel': page === '/room',
    }"
  >
    <aside class="sidebar">
      <div class="sidebar-brand-row">
        <a class="brand" href="#/room" @click.prevent="openHome">nimbo<span>.</span></a
        ><ServerSwitcher
          :servers="personal.servers"
          @open="switchServer"
          @refresh="refreshServerNames"
        />
      </div>
      <a
        class="workspace"
        href="#/room"
        @click.prevent="openHome"
        :aria-label="spaceName + ': открыть первый чат'"
        ><div class="workspace-icon">
          <img
            v-if="workspace.avatar.value"
            :src="workspace.avatar.value"
            alt="Аватар сервера"
          /><template v-else
            >{{ Array.from(spaceName.trim())[0]?.toUpperCase() || 'N'
            }}<b aria-hidden="true">.</b></template
          >
        </div>
        <div>
          <div class="server-heading">
            <strong>{{ spaceName }}</strong
            ><span
              v-if="
                workspace.latency.value &&
                workspace.latency.value > 700 &&
                !workspace.networkError.value
              "
              class="network-latency"
              >{{ workspace.latency.value }} мс</span
            ><span
              class="network-dot"
              :class="networkIndicator.color"
              :title="networkIndicator.label"
              :aria-label="networkIndicator.label"
              role="status"
            ></span>
          </div>
          <span>{{ workspace.description.value || 'Место для своих' }}</span
          ><span
            v-if="
              workspace.networkError.value ||
              call.linkQuality.value === 'lost' ||
              call.linkQuality.value === 'poor'
            "
            class="network-warning"
            role="status"
            >{{ networkIndicator.label }}</span
          >
        </div></a
      >
      <div class="channel-scroll">
        <div
          class="channel-section"
          v-for="group in workspace.sectionOrder.value
            .map((id) => workspace.sections.value.find((s) => s.id === id)!)
            .filter(Boolean)"
          :key="group.id"
        >
          <template v-if="group.kind === 'voice'"
            ><div class="channel-label">
              <span
                class="section-kind-icon"
                role="img"
                aria-label="Голосовые комнаты"
                title="Голосовые комнаты"
                ><AppIcon name="sound" :size="12" /></span
              ><span>{{ group.name }}</span>
            </div>
            <nav class="channels" aria-label="Голосовые комнаты">
              <div
                v-for="room in voiceRooms.filter((c) => (c.section || c.kind) === group.id)"
                :key="room.id"
                class="channel-item"
              >
                <div class="voice-channel-row" :class="{ selected: selectedId === room.id }">
                  <button
                    class="voice-channel-open"
                    :disabled="joining"
                    :aria-label="
                      (connected && localVoiceRoom === room.id ? 'Открыть ' : 'Подключиться: ') +
                      room.name
                    "
                    @click="chooseRoom(room.id)"
                  >
                    <AppIcon :name="room.icon || 'sound'" :size="18" /><span>{{
                      room.name
                    }}</span></button
                  ><span
                    v-if="peopleIn(room.id).length && workspace.duration(room.id)"
                    class="activity-time"
                    title="Время активности комнаты"
                    >{{ workspace.duration(room.id) }}</span
                  ><button
                    v-if="locked && localVoiceRoom === room.id"
                    class="voice-channel-exit"
                    :aria-label="'Выйти из комнаты: ' + room.name"
                    title="Выйти из комнаты"
                    @click="leaveRoom"
                  >
                    <AppIcon name="exit" :size="18" />
                  </button>
                </div>
                <ul v-if="peopleIn(room.id).length" class="channel-people">
                  <li
                    v-for="person in peopleIn(room.id)"
                    :key="person.identity"
                    :class="{
                      speaking: person.speaking,
                      pending: person.pending,
                      reconnecting: person.reconnecting,
                    }"
                    @contextmenu.prevent="openVolume(person, $event)"
                  >
                    <span class="avatar tiny"
                      ><img v-if="avatarFor(person)" :src="avatarFor(person)" alt="" /><template
                        v-else
                        >{{ initials(person.name) }}</template
                      ></span
                    ><span
                      >{{ person.name }}{{ isOwnProfile(person) ? ' · вы' : ''
                      }}<small v-if="person.pending">Подключение…</small
                      ><small v-else-if="person.reconnecting">Переподключается…</small></span
                    ><span class="person-media-hints"
                      ><AppIcon
                        v-if="person.micKnown && !person.mic"
                        name="mic-off"
                        :size="12" /><AppIcon
                        v-if="person.camera"
                        name="camera"
                        :size="12" /><AppIcon v-if="person.screen" name="screen" :size="12"
                    /></span>
                  </li>
                </ul>
              </div>
            </nav> </template
          ><template v-else
            ><div class="channel-label text-channels-label">
              <span
                class="section-kind-icon"
                role="img"
                aria-label="Текстовые каналы"
                title="Текстовые каналы"
                ><svg width="12" height="12" viewBox="0 0 24 24" fill="none" aria-hidden="true">
                  <path
                    d="m9 4-2 16M17 4l-2 16M4 9h16M3 15h16"
                    stroke="currentColor"
                    stroke-width="1.5"
                    stroke-linecap="round"
                  /></svg></span
              ><span>{{ group.name }}</span>
            </div>
            <nav class="channels" aria-label="Текстовые комнаты">
              <button
                v-for="room in textRooms.filter((c) => (c.section || c.kind) === group.id)"
                :key="room.id"
                :class="{ selected: selectedId === room.id }"
                @click="chooseRoom(room.id)"
              >
                <AppIcon :name="room.icon || 'hash'" :size="18" /><span>{{ room.name }}</span
                ><small
                  v-if="workspace.notifications.value.some((n) => n.channel === room.id)"
                  class="channel-mention"
                  aria-label="Есть непрочитанные упоминания или ответы"
                ></small>
              </button></nav
          ></template>
        </div>
        <p v-if="!rooms.length" class="field-help">Комнат пока нет.</p>
      </div>
      <div class="sidebar-space"></div>
      <div class="profile-area">
        <div class="profile-row">
          <button class="profile" aria-label="Мой профиль" @click="navigate('/profile')">
            <span class="avatar small"
              ><img v-if="profileAvatar" :src="profileAvatar" alt="" /><template v-else>{{
                initials(form.name)
              }}</template></span
            ><span class="profile-text"
              ><strong>{{ form.name }}</strong></span
            ></button
          ><button
            class="icon-button"
            aria-label="Настройки"
            title="Настройки"
            @click="navigate('/settings/audio')"
          >
            <AppIcon name="settings" :size="19" />
          </button>
        </div>
      </div>
    </aside>

    <div
      class="main-column"
      :class="{ 'room-view': page === '/room', 'chat-view': page === '/room' && isText }"
    >
      <header class="topbar">
        <div v-if="page === '/room'" class="compact-room-title">
          <AppIcon :name="selectedChannel?.icon || (isText ? 'hash' : 'sound')" :size="21" />
          <h1 class="chat-title-label">
            {{ roomTitle
            }}<span
              v-if="isText && workspace.notifications.value.some((n) => n.channel === selectedId)"
              class="chat-title-dot"
              role="status"
              aria-label="Есть непрочитанные упоминания или ответы"
            ></span>
          </h1>
          <ChatNotificationToggle
            v-if="isText"
            :enabled="chatAlerts.subscriptions.value.includes(selectedId)"
            @toggle="chatAlerts.toggle(selectedId)"
          /><span v-if="!isText" class="room-member-count">{{ peopleIn(selectedId).length }}</span>
        </div>
        <h1 v-else-if="settingsOpen" id="settings-title" class="settings-heading">
          Настройки <span>·</span> {{ sections.find((s) => s.id === section)?.name
          }}<template v-if="section === 'workspace'"
            ><span>·</span><small>{{ members.length }}</small></template
          >
        </h1>
        <h1 v-else id="profile-title" class="settings-heading">Профиль</h1>
        <div id="chat-call-controls" v-show="compactDock" class="chat-call-controls"></div>
        <div class="topbar-actions">
          <button
            v-if="settingsOpen || page === '/profile'"
            class="text-button"
            @click="navigate('/room')"
          >
            <AppIcon name="back" :size="17" />К комнате
          </button>
        </div>
      </header>
      <main v-show="page === '/room'" class="room-stage">
        <div class="room-page">
          <p v-if="workspaceError" class="form-error" role="alert">{{ workspaceError }}</p>
          <p v-if="storageNote" class="form-error" role="alert">{{ storageNote }}</p>
          <div v-show="!isText" class="room-body">
            <section class="stage-content" aria-label="Участники голосовой комнаты">
              <div v-if="(!connected || !viewingCall) && waitingPeople.length" class="waiting-grid">
                <article v-for="person in waitingPeople" :key="person.identity" class="person-card">
                  <div class="avatar">
                    <img v-if="person.avatar" :src="person.avatar" alt="" /><template v-else>{{
                      initials(person.name)
                    }}</template>
                  </div>
                  <strong>{{ person.name }}{{ person.ownProfile ? ' · вы' : '' }}</strong
                  ><small v-if="person.reconnecting">Восстанавливает связь</small>
                </article>
                <button class="join-person" :disabled="joining" @click="chooseRoom(selectedId)">
                  <span class="avatar"><AppIcon name="plus" :size="24" /></span
                  ><span>{{ joining ? 'Подключение…' : 'Присоединиться' }}</span>
                </button>
              </div>
              <button
                v-else-if="!connected || !viewingCall"
                type="button"
                class="empty-room join-room-area"
                :disabled="joining || !selectedChannel || isText"
                :aria-label="'Подключиться к комнате ' + roomTitle"
                @click="chooseRoom(selectedId)"
              >
                <strong>{{
                  joining ? 'Подключаемся к разговору…' : 'Присоединяйтесь к разговору.'
                }}</strong>
              </button>
              <div v-show="connected && viewingCall">
                <CallStage
                  :follow-remote-screen="foreignVoice"
                  @leave="leaveRoom"
                  :screen-audio="call.screenAudio.value"
                  :screen-audio-available="call.screenAudioAvailable.value"
                  :screen-audio-busy="!!call.pending.value"
                  @toggle-screen-audio="call.toggleScreenAudio"
                  :people="stagePeople"
                  @person-menu="openVolume"
                  @moderate-media="moderateMedia"
                  :moderation-busy="mediaModerationBusy"
                  :tiles="videoTiles"
                  :visible="connected && page === '/room' && !isText && viewingCall"
                  :bind-video-host="call.bindVideoHost"
                  :refresh-video-layout="call.refreshVideoLayout"
                />
              </div>
              <p v-if="call.screenAudioNote.value" class="field-help" role="status">
                {{ call.screenAudioNote.value }}
              </p>
              <p v-if="mediaModerationError" class="field-help" role="alert">
                {{ mediaModerationError }}
              </p>
              <p
                v-if="!locked && status && !/^(Выберите комнату|Вы вышли из комнаты)/.test(status)"
                class="room-status"
                role="alert"
              >
                {{ status }}
              </p>
            </section>
          </div>
          <ChatPanel
            :file-drafts="activeFileDrafts"
            :key="activeServer"
            :pending-messages="
              workspace.pendingMessages.value.filter((p) => p.channel === selectedId)
            "
            :retry-pending="workspace.retryPending"
            :dismiss-pending="workspace.dismissPending"
            :edit-message="workspace.editMessage"
            :can-write="workspace.canWrite.value"
            :request="workspace.request"
            :download-file="workspace.downloadFile"
            :load-file="workspace.loadFile"
            :avatars="avatars"
            v-show="isText"
            :members="members"
            :roles="workspace.roles.value"
            :active="page === '/room' && isText"
            :unread-mentions="
              workspace.notifications.value.filter((n) => n.channel === selectedId).map((n) => n.id)
            "
            @read-mentions="workspace.readMentions"
            v-model="draft"
            :messages="messages"
            :can-moderate="workspace.can('manage_chat')"
            :muted-until="workspace.chatMutedUntil.value"
            :remove-message="workspace.removeMessage"
            :channel="selectedId"
            :self-id="workspace.selfId.value"
            :has-more="workspace.hasMore.value"
            :loading-older="workspace.loadingOlder.value"
            :load-older="workspace.loadOlder"
            :sending="sending"
            :reaction-busy="workspace.reactionBusy.value"
            :send-message="sendMessage"
            @react="workspace.react"
            @member-menu="
              (message, event) =>
                openVolume({ identity: message.authorId, name: message.name }, event)
            "
          />
        </div>
      </main>
      <main v-show="settingsOpen" class="settings-page" aria-labelledby="settings-title">
        <div class="settings-layout">
          <nav class="settings-nav" aria-label="Разделы настроек">
            <a
              v-for="item in sections"
              :key="item.id"
              :href="`#/settings/${item.id}`"
              :aria-current="section === item.id ? 'page' : undefined"
              :class="{ selected: section === item.id }"
              @click.prevent="navigate(`/settings/${item.id}`)"
              ><AppIcon :name="item.icon" :size="19" />{{ item.name }}</a
            >
          </nav>
          <div class="settings-content">
            <section v-show="section === 'general'" class="general-settings">
              <label for="language">Язык</label
              ><AppSelect
                id="language"
                label="Язык"
                model-value="ru"
                :options="[{ value: 'ru', label: 'Русский' }]"
              /><StorageSettings
                :key="activeServer"
                v-if="section === 'general' && self?.role === 'owner'"
                :request="workspace.mutate"
              /><AntivirusSettings
                :key="activeServer"
                v-if="section === 'general' && self?.role === 'owner'"
                :request="workspace.request"
              />
            </section>
            <section v-show="section === 'audio'" aria-label="Звук">
              <label for="microphone">Микрофон</label
              ><AppSelect
                id="microphone"
                label="Микрофон"
                :model-value="inputId"
                :options="inputOptions"
                :disabled="busy || joining"
                @update:model-value="devices.selectInput"
              /><label for="output">Наушники / динамики</label
              ><AppSelect
                id="output"
                label="Наушники / динамики"
                :model-value="outputId"
                :options="outputOptions"
                :disabled="!supportsOutput || busy || joining"
                @update:model-value="devices.selectOutput"
              /><button
                class="text-button refresh-devices"
                :disabled="busy || joining"
                @click="devices.refresh(true)"
              >
                {{ busy ? 'Проверяем устройства…' : 'Обновить список устройств' }}
              </button>
              <p v-if="note" class="field-help" role="status">{{ note }}</p>
              <p v-if="locked" class="field-help">
                Фактический источник: {{ devices.actualInput.value }}
              </p>
              <details class="audio-diagnostics">
                <summary>Диагностика устройств</summary>
                <p class="field-help">
                  Последние переключения в этой вкладке. Без записи звука и отправки на сервер.
                </p>
                <button class="text-button" @click="downloadAudioDiagnostics">
                  Скачать журнал
                </button>
              </details>
              <AudioLabPanel
                v-if="AudioLabPanel"
                :devices="devices"
                :disabled="busy || joining || !!pending"
              /><AudioTests
                :noise-engine="
                  devices.lab.suppression.value && devices.lab.engine.value !== 'browser'
                    ? devices.lab.engine.value
                    : undefined
                "
                :capture-options="devices.captureOptions()"
                :input-id="inputId"
                :output-id="outputId"
                :active="page === '/settings/audio'"
                :in-call="locked"
              />
            </section>
            <section v-show="section === 'video'" aria-label="Видео">
              <label for="camera-device">Камера</label
              ><AppSelect
                id="camera-device"
                label="Камера"
                :model-value="cameraId"
                :options="cameraOptions"
                :disabled="busy || joining || !!pending || call.effects.busy.value"
                @update:model-value="call.selectCamera"
              />
              <h3 class="effects-title">Эффекты</h3>
              <EffectPicker
                :strength="call.effects.strength.value"
                @strength="call.effects.setStrength"
                :enabled="call.effects.enabled.value"
                :busy="call.effects.busy.value || !!pending || joining"
                @select="
                  (value) => {
                    if (value !== call.effects.enabled.value) call.effects.toggle();
                  }
                "
              />
              <p v-if="call.effects.note.value" class="field-help" role="status">
                {{ call.effects.note.value }}
              </p>
              <p v-if="!supportsScreen" class="field-help">{{ screenUnavailable }}</p>
            </section>
            <HotkeySettings
              v-if="section === 'hotkeys'"
              :bindings="hotkeys.bindings.value"
              :note="hotkeys.note.value"
              :set="hotkeys.set"
              :reset="hotkeys.reset"
            />
            <div v-if="section === 'workspace'" class="people-settings">
              <div class="people-settings-block">
                <h2>Управление ролями</h2>
                <RoleEditor
                  :roles="workspace.roles.value"
                  :self="self"
                  :members="members"
                  :mutate="workspace.mutate"
                />
              </div>
              <div
                v-if="workspace.can('manage_server')"
                id="invitations-settings"
                class="people-settings-block"
              >
                <h2>Приглашения</h2>
                <InvitationSettings
                  :key="activeServer"
                  :invitations="workspace.invitations.value"
                  :request="workspace.mutate"
                />
              </div>
              <div class="people-settings-block">
                <h2>Управление участниками</h2>
                <WorkspaceSettings
                  :key="activeServer + section"
                  view="workspace"
                  :roles="workspace.roles.value"
                  :avatar="workspace.avatar.value"
                  :sections="workspace.sections.value"
                  :section-order="workspace.sectionOrder.value"
                  :name="spaceName"
                  :description="workspace.description.value"
                  :channels="rooms"
                  :members="members"
                  :avatars="avatars"
                  :self="self"
                  :owner-exists="ownerExists"
                  :mutate="workspace.mutate"
                />
              </div>
            </div>
            <WorkspaceSettings
              :key="activeServer + section"
              v-if="section === 'channels'"
              view="channels"
              :roles="workspace.roles.value"
              :avatar="workspace.avatar.value"
              :sections="workspace.sections.value"
              :section-order="workspace.sectionOrder.value"
              :name="spaceName"
              :description="workspace.description.value"
              :channels="rooms"
              :members="members"
              :avatars="avatars"
              :self="self"
              :owner-exists="ownerExists"
              :mutate="workspace.mutate"
            />
            <FileSettings
              :key="activeServer"
              v-if="section === 'channels' && self?.role === 'owner'"
              :channels="rooms"
              :request="workspace.request"
            />
          </div>
        </div>
      </main>

      <main
        v-if="page === '/profile'"
        class="settings-page profile-page"
        aria-labelledby="profile-title"
      >
        <div class="profile-content">
          <ProfilePanel
            @open-server="switchServer"
            :name="form.name"
            :avatar="profileAvatar"
            :servers="personal.servers"
            :current-role="self?.role"
            :save-servers="saveServers"
            :leave-server="leaveServer"
            :profiles="profileLibrary.entries.value"
            :active-profile-id="profileLibrary.activeId.value"
            :add-profile="addLocalProfile"
            :switch-profile="switchLocalProfile"
            :remove-profile="removeLocalProfile"
            :save="saveProfile"
            :export-file="exportProfile"
            @logout="signOut"
          />
          <p v-if="transferNote" class="field-help" role="status">{{ transferNote }}</p>
        </div>
      </main>
      <!-- Audio is deliberately outside all page visibility containers. -->
      <div ref="audio"></div>
      <div ref="videos" class="media-parking" hidden aria-hidden="true"></div>
      <Teleport defer to="#chat-call-controls" :disabled="!compactDock"
        ><footer v-show="locked" class="call-dock" :class="{ 'call-dock-compact': compactDock }">
          <div class="controls-row">
            <button
              v-if="foreignVoice && voice"
              class="call-origin"
              :title="'Вернуться в звонок · ' + voice.server + ' · ' + voice.title"
              @click="returnToCall"
            >
              <small>{{ voice.server }}</small
              ><strong>{{ voice.title }}</strong>
            </button>
            <div class="control-group icon-controls">
              <button
                :disabled="!connected || !!pending"
                :aria-label="mic ? 'Выключить микрофон' : 'Включить микрофон'"
                :title="mic ? 'Выключить микрофон' : 'Включить микрофон'"
                :aria-pressed="mic"
                :class="{ active: mic }"
                @click="call.toggle('mic')"
              >
                <AppIcon :name="mic ? 'mic' : 'mic-off'" :size="22" />
              </button>
              <button
                :disabled="!connected || !!pending"
                :aria-label="camera ? 'Выключить камеру' : 'Включить камеру'"
                :title="camera ? 'Выключить камеру' : 'Включить камеру'"
                :aria-pressed="camera"
                :class="{ active: camera }"
                @click="call.toggle('camera')"
              >
                <AppIcon :name="camera ? 'camera' : 'camera-off'" :size="22" />
              </button>
              <button
                :disabled="!connected || !!pending || !supportsScreen"
                :aria-label="screen ? 'Остановить показ экрана' : 'Показать экран'"
                :title="
                  screen
                    ? 'Остановить показ экрана'
                    : supportsScreen
                      ? 'Показать экран'
                      : 'Показ экрана недоступен в этом браузере'
                "
                :aria-pressed="screen"
                :class="{ active: screen }"
                @click="call.toggle('screen')"
              >
                <AppIcon name="screen" :size="22" />
              </button>
              <div v-if="camera || screen" class="dock-quality">
                <AppSelect
                  id="call-quality"
                  hide-arrow
                  label="Качество видео и демонстрации"
                  :model-value="sendQuality"
                  :disabled="qualityBusy"
                  :options="[
                    { value: 'auto', label: 'Авто' },
                    { value: '360', label: '360p' },
                    { value: '720', label: '720p' },
                    { value: '1080', label: '1080p' },
                  ]"
                  @update:model-value="call.selectQuality"
                />
              </div>
              <QuickEffects
                :strength="call.effects.strength.value"
                @strength="call.effects.setStrength"
                v-if="camera"
                :enabled="call.effects.enabled.value"
                :busy="call.effects.busy.value || !!pending || joining"
                :note="call.effects.note.value"
                @select="
                  (value) => {
                    if (value !== call.effects.enabled.value) call.effects.toggle();
                  }
                "
              />
              <button
                v-if="needsAudio"
                aria-label="Включить звук"
                title="Включить звук"
                @click="call.playAudio"
              >
                <AppIcon name="headphones" :size="22" />
              </button>
            </div>
            <DockNotice
              :active="locked"
              :activity="pending"
              :message="
                status && !/^(Вы в комнате|Связь восстановлена)/.test(status)
                  ? status
                  : qualityNote && (camera || screen)
                    ? qualityNote
                    : !supportsScreen
                      ? screenUnavailable
                      : ''
              "
            /><button
              class="dock-leave"
              aria-label="Выйти из голосовой комнаты"
              title="Выйти из комнаты"
              @click="leaveRoom"
            >
              <AppIcon name="exit" :size="22" /><span>Выйти</span>
            </button>
          </div>
        </footer></Teleport
      >
    </div>
    <aside
      v-if="page === '/room'"
      id="server-members"
      class="members-sidebar"
      aria-label="Участники сервера"
    >
      <div class="members-panel" :class="{ expanded: membersExpanded }">
        <button
          class="members-toggle"
          :aria-expanded="membersExpanded"
          aria-controls="server-members-list"
          @click="toggleMembers"
        >
          <span
            >Участники <span class="members-count">{{ members.length }}</span></span
          ><AppIcon
            name="chevronDown"
            class="members-arrow"
            :class="{ rotated: membersExpanded }"
            :size="16"
          />
        </button>
        <div class="members-content" :inert="!membersExpanded" :aria-hidden="!membersExpanded">
          <ul id="server-members-list">
            <li
              v-for="person in members"
              :key="person.id"
              tabindex="0"
              @contextmenu.prevent="openVolume({ identity: person.id, name: person.name }, $event)"
              @keydown.shift.f10.prevent="openVolume({ identity: person.id, name: person.name })"
            >
              <span class="avatar small"
                ><img v-if="avatars[person.id]" :src="avatars[person.id]" alt="" /><template
                  v-else
                  >{{ initials(person.name) }}</template
                ></span
              >
              <div>
                <strong>{{ person.name }}{{ person.id === self?.id ? ' · вы' : '' }}</strong
                ><span>{{
                  person.blocked ? 'Доступ заблокирован' : roleLabel(person, workspace.roles.value)
                }}</span>
              </div>
            </li>
          </ul>
        </div>
      </div>
    </aside>
  </div>

  <MemberMenu
    v-if="volumePerson"
    :key="volumePerson.identity + volumePerson.x + volumePerson.y"
    :person="volumePerson"
    :member="menuMember"
    :self="self"
    :x="volumePerson.x"
    :y="volumePerson.y"
    :voice="menuVoice"
    :show-volume="!foreignVoice"
    :volume="call.volumeFor(volumePerson.identity)"
    :set-volume="(value) => call.setParticipantVolume(volumePerson!.identity, value)"
    :moderate="(action, seconds) => moderatePerson(volumePerson!.identity, action, seconds)"
    @close="volumePerson = null"
  />
  <dialog ref="invite" class="modal invite-modal" aria-labelledby="invite-title">
    <button
      class="modal-close icon-button"
      aria-label="Закрыть приглашение"
      @click="invite?.close()"
    >
      <AppIcon name="close" />
    </button>
    <div class="modal-symbol"><AppIcon name="users" :size="25" /></div>
    <span class="eyebrow">{{ roomTitle }}</span>
    <h2 id="invite-title">Позовите своих.</h2>
    <p class="modal-intro">Приглашения создаются в настройках «Участники и роли».</p>
    <label
      >Ссылка на комнату<input
        readonly
        :value="invitationUrl"
        @focus="($event.target as HTMLInputElement).select()" /></label
    ><button class="primary wide" @click="copyInvite">
      <AppIcon name="copy" :size="18" />Скопировать ссылку
    </button>
    <p class="field-help" role="status">
      {{ copied || 'Для нового участника используйте активное приглашение из настроек сервера.' }}
    </p>
  </dialog>
</template>

<style scoped>
.local-profile-home {
  max-width: 960px;
  margin: 0 auto;
  padding: 28px 32px;
}
.local-profile-home > header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 20px;
}
.local-profile-home > h1 {
  margin: 36px 0 56px;
  font-size: 23px;
}
.local-profile-home > .field-help {
  margin-top: 24px;
}
@media (max-width: 650px) {
  .local-profile-home {
    padding: 20px;
  }
  .local-profile-home > h1 {
    margin: 28px 0 40px;
  }
}
</style>

<style scoped>
.auth-form .restore-profile {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 100%;
  padding: 13px 16px;
  margin: 0 0 16px;
  background: #e6e6e6;
  color: #111;
  border: 0;
  border-radius: 11px;
  font-size: 13px;
  cursor: pointer;
}
.auth-form .restore-profile:hover {
  background: #fff;
}
.auth-code-divider {
  font-size: 11px;
  text-align: center;
  color: #777;
  margin: 22px 0;
}
</style>

<style scoped>
.people-settings-block {
  margin-bottom: 36px;
}
.people-settings-block > h2 {
  font-size: 14px;
  font-weight: 500;
  letter-spacing: 0;
  margin: 0 0 16px;
}
.people-settings :deep(section) {
  min-height: 0;
}
.people-settings :deep(.role-editor > .field-help) {
  margin-top: 0;
}
</style>

<style scoped>
.server-switch-status {
  position: fixed;
  z-index: 1000;
  top: 18px;
  left: 50%;
  transform: translateX(-50%);
  display: flex;
  align-items: center;
  gap: 14px;
  max-width: calc(100vw - 32px);
  padding: 10px 16px;
  border-radius: 12px;
  background: #252525;
  color: #ddd;
  box-shadow: 0 8px 28px #0005;
  font-size: 12px;
}
.server-switch-status button {
  font-size: 12px;
  padding: 4px;
}
</style>

<style scoped>
.call-origin {
  display: grid;
  align-content: center;
  max-width: clamp(72px, 12vw, 160px);
  text-align: left;
  padding: 4px 8px;
  background: transparent;
  color: inherit;
}
.call-origin small,
.call-origin strong {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.call-origin small {
  color: var(--muted);
  font-size: 10px;
}
.call-origin strong {
  font-size: 12px;
}
</style>
