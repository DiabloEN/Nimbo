import { ref } from 'vue';
import { serverAddress, type SavedServer } from './profileFile';

export const clientOrigin = typeof location === 'undefined' ? '' : location.origin;
export const activeServer = ref(clientOrigin);
export const remoteServer = () => activeServer.value !== clientOrigin;
export function apiURL(path: string, origin = activeServer.value) {
  if (!path.startsWith('/api/') || path.includes('\\') || path.startsWith('//'))
    throw new Error('Недопустимый путь API.');
  const base = origin || clientOrigin;
  if (base && new URL(path, base).origin !== base) throw new Error('Недопустимый адрес API.');
  return base === clientOrigin ? path : base + path;
}
export function serverFetch(origin: string, path: string, init: RequestInit = {}) {
  const remote = origin !== clientOrigin;
  return fetch(apiURL(path, origin), {
    ...init,
    redirect: 'error',
    credentials: remote ? 'omit' : 'same-origin',
    headers: { ...init.headers, ...(remote ? { 'X-Nimbo-Client': '1' } : {}) },
  });
}
export function apiFetch(path: string, init: RequestInit = {}) {
  return serverFetch(activeServer.value, path, init);
}
export function selectServerOrigin(origin: string) {
  activeServer.value = serverAddress(origin);
  try {
    localStorage.setItem('nimbo-active-server', activeServer.value);
  } catch {}
}
export function restoreServerOrigin(servers: SavedServer[]) {
  try {
    const saved = localStorage.getItem('nimbo-active-server');
    if (saved && servers.some((s) => s.url === saved)) activeServer.value = serverAddress(saved);
  } catch {}
}
// Prepare everything before changing the visible workspace. Neither another
// server's key nor the complete portable profile ever enters this request.
export async function prepareServer(server: SavedServer, signal: AbortSignal) {
  const origin = serverAddress(server.url);
  const response = await serverFetch(origin, '/api/profile-session', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ profileKey: server.profileKey }),
    signal,
  });
  const access = await response.json();
  if (!response.ok) throw new Error(access.error || 'Не удалось войти на сервер.');
  if (
    typeof access.session !== 'string' ||
    !access.session ||
    typeof access.member?.id !== 'string' ||
    typeof access.member?.name !== 'string'
  )
    throw new Error('Некорректный ответ сервера.');
  const state = await serverFetch(origin, '/api/workspace', {
    headers: { Authorization: 'Bearer ' + access.session },
    signal,
  });
  const workspace = await state.json();
  if (!state.ok) throw new Error(workspace.error || 'Не удалось загрузить сервер.');
  if (
    !Array.isArray(workspace.channels) ||
    !Array.isArray(workspace.members) ||
    workspace.selfId !== access.member.id
  )
    throw new Error('Некорректное пространство сервера.');
  return { access, workspace };
}
