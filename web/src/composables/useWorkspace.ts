import { activeServer, apiURL, serverFetch } from './serverConnection';
import { computed, ref } from 'vue';
import { uploadForm } from './uploadForm';
import { canWriteChannel } from './channelPosting';
import { permitted } from './roles';
import type { Mention, MentionNotice } from './mentions';
export type Role = { id: string; name: string; permissions: string[] };

export type ChannelSection = { id: string; name: string; kind: 'voice' | 'text' };
export type PostingPolicy = { mode: 'everyone' | 'roles'; roleIds: string[] };
export type Channel = {
  posting?: PostingPolicy;
  section?: string;
  id: string;
  name: string;
  kind: 'voice' | 'text';
  description: string;
  icon?: string;
};
export type Invitation = {
  id: string;
  code: string;
  label?: string;
  singleUse: boolean;
  createdAt: number;
  expiresAt?: number;
};
export type Member = {
  id: string;
  name: string;
  role: 'owner' | 'admin' | 'member';
  roleIds?: string[];
  permissions?: string[];
  chatMutedUntil?: number;
  avatarVersion?: string;
  blockedUntil?: number;
  blocked?: boolean;
};
export type VoicePerson = {
  identity: string;
  name: string;
  joinedAt: number | string;
  reconnecting?: boolean;
};
export type VoicePresence = { since: number; people: VoicePerson[] };
export type Attachment = { id: string; name: string; size: number; scanned: boolean };
export type PendingMessage = {
  format?: string;
  id: string;
  channel: string;
  text: string;
  replyTo: string;
  mentions: Mention[];
  files: File[];
  status: 'uploading' | 'checking' | 'failed';
  progress: number;
  error: string;
};
export type Message = {
  format?: string;
  editedAt?: number;
  attachment?: Attachment;
  attachments?: Attachment[];
  mentions?: Mention[];
  id: string;
  authorId: string;
  name: string;
  text: string;
  createdAt: number;
  clientId: string;
  reactions?: Record<string, string[]>;
  deleted?: boolean;
  replyTo?: string;
  reply?: { id: string; name: string; text: string; deleted?: boolean };
};
export function useWorkspace(session: { value: string }, renewSession?: () => Promise<boolean>) {
  const pendingMessages = ref<PendingMessage[]>([]);
  const notifications = ref<MentionNotice[]>([]),
    chatActivity = ref<Record<string, { id: string; createdAt: number }>>({});
  const invitations = ref<Invitation[]>([]);
  const roles = ref<Role[]>([]);
  const avatar = ref('');
  let orderPending = false;
  const sectionOrder = ref<string[]>(['voice', 'text']),
    sections = ref<ChannelSection[]>([
      { id: 'voice', name: 'Голосовые', kind: 'voice' },
      { id: 'text', name: 'Текстовые', kind: 'text' },
    ]);
  const expired = ref(false),
    hasMore = ref(false),
    loadingOlder = ref(false),
    reactionBusy = ref<Record<string, boolean>>({});
  const description = ref(''),
    latency = ref<number>(),
    networkError = ref(false);
  const name = ref('Свои люди'),
    channels = ref<Channel[]>([]),
    members = ref<Member[]>([]),
    selfId = ref('');
  const ownerExists = ref(false),
    presence = ref<Record<string, VoicePresence>>({}),
    presenceAvailable = ref(false);
  const error = ref(''),
    messages = ref<Message[]>([]),
    textChannel = ref(''),
    sending = ref(false),
    now = ref(Date.now()),
    ready = ref(false);
  const self = computed(() => members.value.find((m) => m.id === selfId.value));
  const can = (permission: string) => permitted(self.value, permission);
  const canWrite = computed(() =>
    canWriteChannel(
      channels.value.find((c) => c.id === textChannel.value),
      self.value,
    ),
  );
  const canManage = computed(() => can('manage_channels'));
  const chatMutedUntil = computed(() =>
    (self.value?.chatMutedUntil || 0) > now.value ? self.value!.chatMutedUntil! : 0,
  );
  let generation = 0,
    controller: AbortController | undefined,
    timer: ReturnType<typeof setTimeout> | undefined,
    clock: ReturnType<typeof setInterval> | undefined;
  let refreshSequence = 0,
    messageSequence = 0;
  let offset = 0,
    draftAttempt:
      | {
          channel: string;
          text: string;
          replyTo: string;
          mentions: string;
          format: string;
          key: string;
          files?: File[];
        }
      | undefined;
  async function request(
    path: string,
    method = 'GET',
    body?: unknown,
    signal?: AbortSignal,
    progress?: (percent: number) => void,
  ) {
    const current = generation,
      originalToken = session.value,
      origin = activeServer.value;
    async function send() {
      if (current !== generation || origin !== activeServer.value)
        throw new Error('Запрос отменён.');
      try {
        if (body instanceof FormData && progress && typeof XMLHttpRequest !== 'undefined')
          return await uploadForm(
            apiURL(path, origin),
            body,
            session.value,
            signal
              ? AbortSignal.any([signal, AbortSignal.timeout(240000)])
              : AbortSignal.timeout(240000),
            progress,
          );
        return await serverFetch(origin, path, {
          method,
          headers: {
            ...(body instanceof FormData ? {} : { 'Content-Type': 'application/json' }),
            Authorization: `Bearer ${session.value}`,
          },
          body:
            body instanceof FormData ? body : body === undefined ? undefined : JSON.stringify(body),
          signal: signal
            ? AbortSignal.any([
                signal,
                AbortSignal.timeout(body instanceof FormData ? 240000 : 10000),
              ])
            : AbortSignal.timeout(body instanceof FormData ? 240000 : 10000),
        });
      } catch {
        throw new Error('Сервер не отвечает. Пробуем восстановить соединение…');
      }
    }
    let response = await send();
    if (response.status === 401 && renewSession && current === generation && session.value) {
      // An expired API token does not revoke the remembered browser session.
      const recovered = session.value !== originalToken || (await renewSession());
      if (current !== generation || signal?.aborted) throw new Error('Запрос отменён.');
      if (recovered) response = await send();
    }
    if (response.status === 401 && current === generation) expired.value = true;
    let data;
    try {
      data = await response.json();
    } catch {
      throw new Error('Сервер временно недоступен. Пробуем восстановить соединение…');
    }
    if (current !== generation || origin !== activeServer.value) throw new Error('Запрос отменён.');
    if (!response.ok) throw new Error(data.error || 'Сервис недоступен. Попробуйте ещё раз.');
    return data;
  }
  async function refresh() {
    const current = generation,
      token = session.value,
      sequence = ++refreshSequence;
    if (!token) return;
    const started = performance.now();
    let data;
    try {
      data = await request('/api/workspace', 'GET', undefined, controller?.signal);
    } catch (e) {
      if (current !== generation) return;
      throw e;
    }
    if (current !== generation || token !== session.value || sequence !== refreshSequence) return;
    latency.value = Math.round(performance.now() - started);
    applySnapshot(data);
  }
  function applySnapshot(data: any) {
    invitations.value = data.invitations || [];
    chatActivity.value = data.chatActivity || {};
    notifications.value = data.notifications || [];
    roles.value = data.roles || [];
    avatar.value = data.avatar || '';
    description.value = data.description || '';
    networkError.value = false;
    if (!orderPending) {
      sectionOrder.value = data.sectionOrder || ['voice', 'text'];
      sections.value = data.sections || sections.value;
      channels.value = data.channels.map((c: Channel) => ({
        ...c,
        section: data.channelSections?.[c.id] || c.kind,
      }));
    }
    name.value = data.name;
    members.value = data.members;
    selfId.value = data.selfId;
    ownerExists.value = data.ownerExists;
    presence.value = data.presence;
    presenceAvailable.value = data.presenceAvailable;
    offset = data.serverTime - Date.now();
    now.value = Date.now() + offset;
    ready.value = true;
    error.value = '';
  }
  async function historyFrom(channel: string, from: string | undefined, sequence: number) {
    const signal = controller?.signal,
      base = '/api/messages?paged=1&channel=' + encodeURIComponent(channel);
    let data = await request(
      base + (from ? '&from=' + encodeURIComponent(from) : ''),
      'GET',
      undefined,
      signal,
    );
    const page: Message[] = Array.isArray(data) ? data : [...data.messages];
    const more = !!data.hasMore;
    const cursors = new Set<string>();
    while (
      data.nextFrom &&
      sequence === messageSequence &&
      channel === textChannel.value &&
      !signal?.aborted
    ) {
      const cursor = String(data.nextFrom);
      if (cursors.has(cursor)) throw new Error('Страница истории устарела.');
      cursors.add(cursor);
      data = await request(
        base + '&from=' + encodeURIComponent(cursor) + '&continuation=1',
        'GET',
        undefined,
        signal,
      );
      page.push(...data.messages);
    }
    return { messages: page, hasMore: more };
  }
  async function refreshMessages() {
    const channel = textChannel.value,
      current = generation,
      sequence = ++messageSequence;
    if (!channel || !session.value) return;
    const from = messages.value[0]?.id;
    const data = await historyFrom(channel, from, sequence);
    if (current === generation && channel === textChannel.value && sequence === messageSequence) {
      messages.value = data.messages;
      hasMore.value = !!data.hasMore;
      pendingMessages.value = pendingMessages.value.filter(
        (p) => p.channel !== channel || !messages.value.some((m) => m.clientId === p.id),
      );
    }
  }
  async function tick(current: number) {
    if (current !== generation) return;
    try {
      await refresh();
    } catch (e) {
      if (current === generation) {
        networkError.value = true;
        error.value = (e as Error).message;
        presenceAvailable.value = false;
      }
    }
    try {
      await refreshMessages();
    } catch (e) {
      if (current === generation) error.value = (e as Error).message;
    }
    if (current === generation)
      timer = setTimeout(() => void tick(current), document.hidden ? 10000 : 2500);
  }
  function start(snapshot?: unknown) {
    stop();
    if (snapshot) applySnapshot(snapshot);
    expired.value = false;
    controller = new AbortController();
    clock = setInterval(() => (now.value = Date.now() + offset), 1000);
    void tick(generation);
  }
  function stop() {
    ++generation;
    controller?.abort();
    clearTimeout(timer);
    clearInterval(clock);
    ready.value = false;
    selfId.value = '';
    sending.value = false;
    loadingOlder.value = false;
    reactionBusy.value = {};
    orderPending = false;
    invitations.value = [];
    chatActivity.value = {};
    notifications.value = [];
    messages.value = [];
    members.value = [];
    presence.value = {};
    textChannel.value = '';
    presenceAvailable.value = false;
    error.value = '';
    draftAttempt = undefined;
    pendingMessages.value = [];
    readingMentions.clear();
  }
  async function selectText(id: string) {
    if (textChannel.value === id) return;
    textChannel.value = id;
    messages.value = [];
    hasMore.value = false;
    try {
      await refreshMessages();
    } catch (e) {
      error.value = (e as Error).message;
    }
  }
  async function mutate(path: string, method: string, body?: unknown) {
    if (path !== '/api/channel-order') {
      const current = generation,
        result = await request(path, method, body);
      if (current === generation) await refresh();
      return result;
    }
    if (orderPending) throw new Error('Дождитесь сохранения порядка.');
    const previousChannels = channels.value,
      previousSections = sectionOrder.value,
      current = generation;
    const order = body as { channels: string[]; sections: string[] };
    orderPending = true;
    ++refreshSequence;
    channels.value = order.channels
      .map((id) => previousChannels.find((c) => c.id === id)!)
      .filter(Boolean);
    sectionOrder.value = [...order.sections];
    try {
      return await request(path, method, body);
    } catch (e) {
      if (current === generation) {
        channels.value = previousChannels;
        sectionOrder.value = previousSections;
      }
      throw e;
    } finally {
      if (current === generation) {
        orderPending = false;
        ++refreshSequence;
        void refresh().catch(() => {});
      }
    }
  }
  async function send(
    text: string,
    replyTo = '',
    mentions: Mention[] = [],
    files?: File[],
    retry?: PendingMessage,
    format = '',
  ) {
    if (!canWrite.value || sending.value || (!text.trim() && !files?.length) || !textChannel.value)
      return false;
    const channel = textChannel.value,
      current = generation;
    sending.value = true;
    if (retry)
      draftAttempt = {
        channel,
        text,
        replyTo,
        mentions: JSON.stringify(mentions),
        format,
        key: retry.id,
        files: files?.slice(),
      };
    if (
      draftAttempt?.channel !== channel ||
      draftAttempt?.text !== text ||
      draftAttempt?.format !== format ||
      draftAttempt?.replyTo !== replyTo ||
      draftAttempt?.mentions !== JSON.stringify(mentions) ||
      draftAttempt?.files?.length !== files?.length ||
      files?.some((file, i) => file !== draftAttempt?.files?.[i])
    ) {
      const bytes = crypto.getRandomValues(new Uint8Array(32));
      const key = btoa(String.fromCharCode(...bytes))
        .replaceAll('+', '-')
        .replaceAll('/', '_')
        .replaceAll('=', '');
      draftAttempt = {
        channel,
        text,
        replyTo,
        mentions: JSON.stringify(mentions),
        format,
        key,
        files: files?.slice(),
      };
    }
    let pending: PendingMessage | undefined;
    if (files?.length) {
      pending = pendingMessages.value.find((p) => p.id === draftAttempt!.key);
      if (!pending) {
        pendingMessages.value.push({
          id: draftAttempt.key,
          channel,
          text,
          replyTo,
          mentions: [...mentions],
          format,
          files: [...files],
          status: 'uploading',
          progress: 0,
          error: '',
        });
        pending = pendingMessages.value.at(-1)!;
      }
      pending.status = 'uploading';
      pending.progress = 0;
      pending.error = '';
    }
    try {
      let accepted: Message;
      if (files?.length) {
        const body = new FormData();
        for (const file of files) body.append('file', file);
        body.set('text', text);
        if (format) body.set('format', format);
        body.set('replyTo', replyTo);
        body.set('clientId', draftAttempt.key);
        body.set('mentions', JSON.stringify(mentions));
        accepted = await request(
          '/api/channels/' + encodeURIComponent(channel) + '/files',
          'POST',
          body,
          controller?.signal,
          (percent) => {
            if (pending) {
              pending.progress = percent;
              pending.status = percent === 100 ? 'checking' : 'uploading';
            }
          },
        );
      } else
        accepted = await request('/api/messages', 'POST', {
          channel,
          text,
          replyTo,
          mentions,
          ...(format && { format }),
          clientId: draftAttempt.key,
        });
      if (current !== generation) return false;
      if (pending)
        pendingMessages.value = pendingMessages.value.filter((p) => p.id !== pending!.id);
      draftAttempt = undefined;
      error.value = '';
      ++messageSequence;
      if (channel === textChannel.value && !messages.value.some((m) => m.id === accepted.id))
        messages.value = [...messages.value, accepted];
      void refreshMessages().catch(() => {
        /* Accepted message remains visible; the next poll reconciles history. */
      });
      return true;
    } catch (e) {
      if (current === generation) {
        if (pending && !pendingMessages.value.some((p) => p.id === pending!.id)) {
          error.value = '';
          return true;
        }
        error.value = (e as Error).message;
        if (pending) {
          pending.status = 'failed';
          pending.error = error.value;
        }
      }
      return false;
    } finally {
      if (current === generation) sending.value = false;
    }
  }
  async function retryPending(p: PendingMessage) {
    if (p.channel !== textChannel.value || sending.value || p.status !== 'failed') return;
    await send(p.text, p.replyTo, p.mentions, p.files, p, p.format);
  }
  function dismissPending(id: string) {
    pendingMessages.value = pendingMessages.value.filter(
      (p) => p.id !== id || p.status !== 'failed',
    );
  }
  async function editMessage(message: Message, text: string, mentions: Mention[], format = '') {
    const channel = textChannel.value,
      current = generation;
    const result: Message = await request(
      '/api/messages/' + encodeURIComponent(message.id),
      'PATCH',
      {
        channel,
        text,
        mentions,
        previousText: message.text,
        ...((format || message.format) && { format, previousFormat: message.format || '' }),
      },
    );
    if (current !== generation || channel !== textChannel.value) return;
    ++messageSequence;
    messages.value = messages.value.map((m) => (m.id === result.id ? result : m));
  }
  async function loadOlder() {
    if (loadingOlder.value || !hasMore.value || !messages.value.length) return;
    const channel = textChannel.value,
      current = generation,
      before = messages.value[0]!.id;
    loadingOlder.value = true;
    try {
      const data = await request(
        '/api/messages?paged=1&channel=' +
          encodeURIComponent(channel) +
          '&before=' +
          encodeURIComponent(before),
      );
      if (current !== generation || channel !== textChannel.value) return;
      ++messageSequence;
      const ids = new Set(messages.value.map((m) => m.id));
      messages.value = [...data.messages.filter((m: Message) => !ids.has(m.id)), ...messages.value];
      hasMore.value = data.hasMore;
    } catch (e) {
      if (current === generation) error.value = (e as Error).message;
    } finally {
      if (current === generation) loadingOlder.value = false;
    }
  }
  async function react(message: Message, emoji: string) {
    const key = message.id + emoji;
    if (reactionBusy.value[key]) return;
    reactionBusy.value[key] = true;
    const channel = textChannel.value,
      current = generation,
      active = !message.reactions?.[emoji]?.includes(selfId.value);
    try {
      const result: Message = await request(
        '/api/messages/' + encodeURIComponent(message.id) + '/reactions',
        'PUT',
        { channel, emoji, active },
      );
      if (current !== generation || channel !== textChannel.value) return;
      ++messageSequence;
      messages.value = messages.value.map((m) => (m.id === result.id ? result : m));
      error.value = '';
    } catch (e) {
      if (current === generation) error.value = (e as Error).message;
    } finally {
      if (current === generation) delete reactionBusy.value[key];
    }
  }
  async function removeMessage(message: Message) {
    const channel = textChannel.value,
      current = generation;
    try {
      const result: Message = await request(
        '/api/messages/' +
          encodeURIComponent(message.id) +
          '?channel=' +
          encodeURIComponent(channel),
        'DELETE',
      );
      if (current !== generation || channel !== textChannel.value) return;
      ++messageSequence;
      messages.value = messages.value
        .filter((m) => m.id !== result.id)
        .map((m) =>
          m.replyTo === result.id
            ? { ...m, reply: { id: result.id, name: '', text: '', deleted: true } }
            : m,
        );
      error.value = '';
    } catch (e) {
      if (current === generation) error.value = (e as Error).message;
      throw e;
    }
  }
  async function loadFile(file: Attachment) {
    const origin = activeServer.value,
      current = generation;
    const get = () => {
      if (current !== generation) throw new Error('Запрос отменён.');
      return serverFetch(origin, '/api/files/' + encodeURIComponent(file.id), {
        headers: { Authorization: 'Bearer ' + session.value },
        signal: AbortSignal.any([
          controller?.signal || new AbortController().signal,
          AbortSignal.timeout(120000),
        ]),
      });
    };
    let response = await get();
    if (response.status === 401 && current === generation && renewSession && (await renewSession()))
      response = await get();
    if (!response.ok) throw new Error('Файл недоступен. Возможно, сообщение уже удалено.');
    const blob = await response.blob();
    if (current !== generation || origin !== activeServer.value) throw new Error('Запрос отменён.');
    return blob;
  }
  async function downloadFile(file: Attachment) {
    const blob = await loadFile(file),
      url = URL.createObjectURL(blob),
      a = document.createElement('a');
    a.href = url;
    a.download = file.name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
  const readingMentions = new Set<string>();
  async function readMentions(ids: string[]) {
    ids = ids.filter((id) => !readingMentions.has(id));
    if (!ids.length) return;
    ids.forEach((id) => readingMentions.add(id));
    const current = generation;
    try {
      await request('/api/mentions/read', 'POST', { ids });
      if (current === generation) {
        ++refreshSequence;
        notifications.value = notifications.value.filter((n) => !ids.includes(n.id));
      }
    } catch (e) {
      if (current === generation) error.value = (e as Error).message;
    } finally {
      if (current === generation) ids.forEach((id) => readingMentions.delete(id));
    }
  }
  async function revealMessage(channel: string, id: string) {
    await selectText(channel);
    if (textChannel.value !== channel || messages.value.some((m) => m.id === id)) return;
    const current = generation,
      sequence = ++messageSequence;
    try {
      const data = await historyFrom(channel, id, sequence);
      if (current === generation && channel === textChannel.value && sequence === messageSequence) {
        messages.value = data.messages;
        hasMore.value = !!data.hasMore;
      }
    } catch (e) {
      if (current === generation) error.value = (e as Error).message;
    }
  }
  function duration(id: string) {
    const since = presence.value[id]?.since;
    if (!presenceAvailable.value || !since || !presence.value[id]?.people.length) return '';
    const seconds = Math.max(0, Math.floor((now.value - since) / 1000));
    const h = Math.floor(seconds / 3600),
      m = Math.floor((seconds % 3600) / 60),
      s = seconds % 60;
    return `${h ? h + ':' : ''}${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
  }
  return {
    pendingMessages,
    retryPending,
    dismissPending,
    editMessage,
    canWrite,
    request,
    loadFile,
    downloadFile,
    invitations,
    sections,
    chatActivity,
    notifications,
    readMentions,
    revealMessage,
    roles,
    can,
    chatMutedUntil,
    removeMessage,
    avatar,
    sectionOrder,
    hasMore,
    loadingOlder,
    reactionBusy,
    loadOlder,
    react,
    description,
    latency,
    networkError,
    expired,
    name,
    channels,
    members,
    selfId,
    self,
    ownerExists,
    presence,
    presenceAvailable,
    canManage,
    error,
    messages,
    textChannel,
    sending,
    ready,
    start,
    stop,
    refresh,
    selectText,
    mutate,
    send,
    duration,
  };
}
