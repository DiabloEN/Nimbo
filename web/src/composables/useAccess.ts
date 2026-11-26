import { apiFetch, remoteServer, activeServer, serverFetch } from './serverConnection';
import { ref } from 'vue';

// Password and API bearer stay in memory; the server remembers this browser
// using a revocable HttpOnly cookie. Profile keys are separately transferable.
export function useAccess() {
  const session = ref(''),
    profileKey = ref(''),
    storageNote = ref(''),
    member = ref<{ id?: string; name: string; avatar?: string } | null>(null);
  try {
    profileKey.value = localStorage.getItem('nimbo-profile-key') || '';
  } catch {
    /* storage is optional */
  }
  const authenticated = ref(false),
    authenticating = ref(false),
    error = ref('');
  const restoring = ref(false),
    restoreFailed = ref(false);
  let renewal: Promise<boolean> | undefined;
  let controller: AbortController | undefined;
  let generation = 0;
  async function restore({ background = false, savedKey = '', profileOnly = false } = {}) {
    const origin = activeServer.value;
    const current = ++generation;
    controller?.abort();
    if (!background) restoring.value = true;
    error.value = '';
    controller = new AbortController();
    const requestController = controller;
    const timeout = setTimeout(() => requestController.abort(), 10000);
    try {
      try {
        if (localStorage.getItem('nimbo-signed-out:' + origin) === '1') {
          restoreFailed.value = false;
          return false;
        }
      } catch {
        /* storage is optional */
      }
      const signal = requestController.signal;
      let response: Response | undefined;
      const profileRequest = {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ profileKey: savedKey }),
        signal,
      };
      if (!remoteServer()) {
        if (savedKey) {
          response = await serverFetch(origin, '/api/session/profile', profileRequest);
        } else if (!profileOnly) {
          response = await serverFetch(origin, '/api/session', { signal });
        }
        if (current !== generation || activeServer.value !== origin) return false;
      }
      // Older servers have no cookie-and-profile verification route.
      if (savedKey && (!response || [401, 404, 405].includes(response.status))) {
        response = await serverFetch(origin, '/api/profile-session', profileRequest);
        if (current !== generation || activeServer.value !== origin) return false;
      }
      if (!response || response.status === 401 || response.status === 403) {
        if (response && (savedKey || response.status === 403)) {
          const denial = await response.json().catch(() => null);
          if (current !== generation || activeServer.value !== origin) return false;
          error.value =
            typeof denial?.error === 'string' ? denial.error : 'Сохранённый вход недоступен.';
        }
        restoreFailed.value = false;
        return false;
      }
      if (!response.ok) throw new Error();
      const result = await response.json();
      if (current !== generation || activeServer.value !== origin) return false;
      if (
        !result.session ||
        typeof result.session !== 'string' ||
        typeof result.member?.name !== 'string'
      )
        throw new Error('Некорректный ответ сервера при восстановлении сессии.');
      if (background && member.value?.id && member.value.id !== result.member.id) {
        restoreFailed.value = false;
        error.value = 'Текущая сессия принадлежит другому профилю.';
        return false;
      }
      session.value = result.session;
      member.value = result.member;
      authenticated.value = true;
      restoreFailed.value = false;
      return true;
    } catch {
      if (current === generation) {
        restoreFailed.value = true;
        error.value = 'Сервер пока не отвечает. Повторяем подключение — заново входить не нужно.';
      }
      return false;
    } finally {
      clearTimeout(timeout);
      if (current === generation) {
        restoring.value = false;
        controller = undefined;
      }
    }
  }
  function renew() {
    if (!renewal) {
      const pending = restore({
        background: true,
        savedKey: remoteServer() ? profileKey.value : '',
      }).finally(() => {
        if (renewal === pending) renewal = undefined;
      });
      renewal = pending;
    }
    return renewal;
  }
  async function signOut() {
    const origin = activeServer.value,
      remote = remoteServer();
    ++generation;
    controller?.abort();
    try {
      const response = await apiFetch(remote ? '/api/access-session' : '/api/session', {
        method: 'DELETE',
        headers: { Authorization: 'Bearer ' + session.value },
        signal: AbortSignal.timeout(10000),
      });
      if (!response.ok) throw new Error();
      try {
        localStorage.setItem('nimbo-signed-out:' + origin, '1');
      } catch {}
      if (origin !== activeServer.value) return true;
      reset();
      return true;
    } catch {
      error.value = 'Не удалось выйти на сервере. Проверьте соединение и повторите выход.';
      return false;
    }
  }
  async function signIn(name: string, password: string, fromProfile = false) {
    if (authenticating.value) return false;
    const current = ++generation;
    controller = new AbortController();
    const requestController = controller;
    const timeout = setTimeout(() => requestController.abort(), 10000);
    authenticating.value = true;
    error.value = '';
    try {
      const response = await apiFetch(fromProfile ? '/api/profile-session' : '/api/auth', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(
          fromProfile
            ? { profileKey: profileKey.value }
            : {
                name: name.trim(),
                password,
                ...(profileKey.value ? { profileKey: profileKey.value } : {}),
              },
        ),
        signal: requestController.signal,
      });
      const result = await response.json();
      if (current !== generation) return false;
      if (!response.ok) {
        error.value = result.error || 'Не удалось войти.';
        return false;
      }
      session.value = result.session || '';
      member.value = result.member || null;
      if (result.profileKey) {
        profileKey.value = result.profileKey;
        try {
          localStorage.setItem('nimbo-profile-key', result.profileKey);
        } catch {
          storageNote.value = 'Браузер не сохранил профиль. Скачайте файл в разделе «Мой профиль».';
        }
      }
      authenticated.value = true;
      try {
        localStorage.removeItem('nimbo-signed-out:' + activeServer.value);
      } catch {}
      return true;
    } catch {
      if (current === generation)
        error.value = 'Не удалось связаться с сервером. Попробуйте ещё раз.';
      return false;
    } finally {
      clearTimeout(timeout);
      if (current === generation) {
        authenticating.value = false;
        controller = undefined;
      }
    }
  }
  function reset() {
    renewal = undefined;
    ++generation;
    controller?.abort();
    controller = undefined;
    authenticated.value = false;
    session.value = '';
    member.value = null;
    restoring.value = false;
    restoreFailed.value = false;
    authenticating.value = false;
    error.value = '';
  }
  return {
    session,
    profileKey,
    member,
    storageNote,
    authenticated,
    authenticating,
    restoring,
    restoreFailed,
    error,
    signIn,
    restore,
    renew,
    signOut,
    reset,
  };
}
