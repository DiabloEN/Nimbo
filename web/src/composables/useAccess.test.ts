import { afterEach, expect, it, vi } from 'vitest';
import { useAccess } from './useAccess';
import { activeServer } from './serverConnection';
afterEach(() => vi.unstubAllGlobals());
it('rejects an invalid shared code, then accepts a verified name without opening media', async () => {
  const fetchMock = vi
    .fn()
    .mockResolvedValueOnce({ ok: false, json: async () => ({ error: 'Неверный код доступа' }) })
    .mockResolvedValueOnce({ ok: true, json: async () => ({ name: 'Tester' }) });
  vi.stubGlobal('fetch', fetchMock);
  const access = useAccess();
  expect(await access.signIn('Tester', 'wrong')).toBe(false);
  expect(access.authenticated.value).toBe(false);
  expect(access.error.value).toBe('Неверный код доступа');
  expect(await access.signIn(' Tester ', 'valid-test-code')).toBe(true);
  expect(fetchMock.mock.calls[1]![0]).toBe('/api/auth');
  expect(JSON.parse(fetchMock.mock.calls[1]![1].body)).toEqual({
    name: 'Tester',
    password: 'valid-test-code',
  });
  expect(access.authenticated.value).toBe(true);
  access.reset();
  expect(access.authenticated.value).toBe(false);
});
it('cannot restore access from a late response after sign-out', async () => {
  let resolve!: (value: unknown) => void;
  vi.stubGlobal(
    'fetch',
    vi.fn(
      () =>
        new Promise((done) => {
          resolve = done;
        }),
    ),
  );
  const access = useAccess();
  const pending = access.signIn('Tester', 'test-code');
  access.reset();
  resolve({ ok: true, json: async () => ({ name: 'Tester' }) });
  expect(await pending).toBe(false);
  expect(access.authenticated.value).toBe(false);
  expect(access.authenticating.value).toBe(false);
});
it('restores a server-verified cookie session without a password and revokes it on logout', async () => {
  const fetchMock = vi
    .fn()
    .mockResolvedValueOnce({
      ok: true,
      json: async () => ({ session: 'bearer', member: { name: 'Admin' } }),
    })
    .mockResolvedValueOnce({ ok: true });
  vi.stubGlobal('fetch', fetchMock);
  const access = useAccess();
  expect(await access.restore()).toBe(true);
  expect(access.member.value?.name).toBe('Admin');
  expect(access.session.value).toBe('bearer');
  expect(fetchMock.mock.calls[0]![0]).toBe('/api/session');
  expect(await access.signOut()).toBe(true);
  expect(access.authenticated.value).toBe(false);
  expect(fetchMock.mock.calls[1]![1].method).toBe('DELETE');
});
it('does not trust an expired session or a restore response arriving after reset', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue({ status: 401, ok: false }));
  const expired = useAccess();
  expect(await expired.restore()).toBe(false);
  expect(expired.authenticated.value).toBe(false);
  let resolve!: (value: unknown) => void;
  vi.stubGlobal(
    'fetch',
    vi.fn(
      () =>
        new Promise((done) => {
          resolve = done;
        }),
    ),
  );
  const access = useAccess(),
    pending = access.restore();
  access.reset();
  resolve({ ok: true, json: async () => ({ session: 'late', member: { name: 'Late' } }) });
  expect(await pending).toBe(false);
  expect(access.authenticated.value).toBe(false);
});

it('revokes a remote device with its bearer before clearing local access', async () => {
  const { activeServer } = await import('./serverConnection');
  const original = activeServer.value;
  try {
    activeServer.value = 'https://remote.example';
    const fetcher = vi
      .fn()
      .mockResolvedValueOnce({ ok: false })
      .mockResolvedValueOnce({ ok: true });
    vi.stubGlobal('fetch', fetcher);
    const access = useAccess();
    access.authenticated.value = true;
    access.session.value = 'device-token';
    expect(await access.signOut()).toBe(false);
    expect(access.session.value).toBe('device-token');
    expect(await access.signOut()).toBe(true);
    expect(fetcher).toHaveBeenCalledWith(
      'https://remote.example/api/access-session',
      expect.objectContaining({
        method: 'DELETE',
        credentials: 'omit',
        headers: expect.objectContaining({ Authorization: 'Bearer device-token' }),
      }),
    );
    expect(access.session.value).toBe('');
  } finally {
    activeServer.value = original;
  }
});

const validSession = () => Response.json({ session: 'verified', member: { name: 'Tester' } });
const denied = (status = 401) => Response.json({ error: 'Доступ отозван' }, { status });

it('only tries the saved server key when the cookie is missing', async () => {
  const fetcher = vi
    .fn()
    .mockResolvedValueOnce(validSession())
    .mockResolvedValueOnce(denied())
    .mockResolvedValueOnce(validSession());
  vi.stubGlobal('fetch', fetcher);
  const access = useAccess();
  expect(await access.restore({ savedKey: 'server-key' })).toBe(true);
  expect(fetcher).toHaveBeenCalledTimes(1);
  access.reset();
  const pending = access.restore({ savedKey: 'server-key' });
  expect(access.restoring.value).toBe(true);
  expect(await pending).toBe(true);
  expect(fetcher.mock.calls.map(([url]) => url)).toEqual([
    '/api/session/profile',
    '/api/session/profile',
    '/api/profile-session',
  ]);
  expect(JSON.parse(fetcher.mock.calls[2]![1].body)).toEqual({ profileKey: 'server-key' });
  expect(access.member.value?.name).toBe('Tester');
  expect(access.restoring.value).toBe(false);
});

it('keeps a network failure retryable and accepts a definitive denial without another retry', async () => {
  const fetcher = vi
    .fn()
    .mockResolvedValueOnce(new Response(null, { status: 503 }))
    .mockResolvedValueOnce(denied())
    .mockResolvedValueOnce(denied())
    .mockResolvedValueOnce(denied(403));
  vi.stubGlobal('fetch', fetcher);
  const access = useAccess();
  expect(await access.restore({ savedKey: 'server-key' })).toBe(false);
  expect(access.restoreFailed.value).toBe(true);
  expect(fetcher).toHaveBeenCalledTimes(1);
  expect(await access.restore({ savedKey: 'server-key' })).toBe(false);
  expect(access.restoreFailed.value).toBe(false);
  expect(access.error.value).toBe('Доступ отозван');
  expect(await access.restore({ savedKey: 'server-key' })).toBe(false);
  expect(access.restoreFailed.value).toBe(false);
  expect(fetcher).toHaveBeenCalledTimes(4);
  expect(access.error.value).toBe('Доступ отозван');
});

it('never uses a different profile cookie for an explicit server link', async () => {
  const fetcher = vi.fn().mockResolvedValueOnce(validSession());
  vi.stubGlobal('fetch', fetcher);
  const access = useAccess();
  expect(await access.restore({ profileOnly: true })).toBe(false);
  expect(fetcher).not.toHaveBeenCalled();
  expect(await access.restore({ profileOnly: true, savedKey: 'selected-key' })).toBe(true);
  expect(fetcher.mock.calls.map(([url]) => url)).toEqual(['/api/session/profile']);
});

it.each([404, 405])(
  'can return to an older server without the verification endpoint (%s)',
  async (status) => {
    const fetcher = vi
      .fn()
      .mockResolvedValueOnce(new Response(null, { status }))
      .mockResolvedValueOnce(validSession());
    vi.stubGlobal('fetch', fetcher);
    const access = useAccess();
    expect(await access.restore({ savedKey: 'selected-key' })).toBe(true);
    expect(fetcher.mock.calls.map(([url]) => url)).toEqual([
      '/api/session/profile',
      '/api/profile-session',
    ]);
  },
);

it('honours explicit same-origin sign-out on reload until a deliberate sign-in', async () => {
  const storage = new Map();
  vi.stubGlobal('localStorage', {
    getItem: (key: string) => storage.get(key),
    setItem: (key: string, value: string) => storage.set(key, value),
    removeItem: (key: string) => storage.delete(key),
  });
  const fetcher = vi
    .fn()
    .mockResolvedValueOnce(validSession())
    .mockResolvedValueOnce(new Response())
    .mockResolvedValueOnce(validSession())
    .mockResolvedValueOnce(validSession());
  vi.stubGlobal('fetch', fetcher);
  const access = useAccess();
  expect(await access.restore()).toBe(true);
  expect(await access.signOut()).toBe(true);
  const reopened = useAccess();
  expect(await reopened.restore({ savedKey: 'server-key' })).toBe(false);
  expect(fetcher).toHaveBeenCalledTimes(2);
  expect(await reopened.signIn('Tester', 'invite')).toBe(true);
  expect(await useAccess().restore()).toBe(true);
});

it('retries the selected remote key after a transport failure and discards a late response from the old server', async () => {
  const original = activeServer.value;
  let complete!: (response: Response) => void;
  const fetcher = vi
    .fn()
    .mockRejectedValueOnce(new TypeError('Network error'))
    .mockImplementationOnce(
      () =>
        new Promise<Response>((resolve) => {
          complete = resolve;
        }),
    )
    .mockResolvedValueOnce(validSession());
  vi.stubGlobal('fetch', fetcher);
  try {
    activeServer.value = 'https://first.example';
    const access = useAccess();
    expect(await access.restore({ savedKey: 'first-key' })).toBe(false);
    expect(access.restoreFailed.value).toBe(true);
    const late = access.restore({ savedKey: 'first-key' });
    expect(access.restoring.value).toBe(true);
    activeServer.value = 'https://second.example';
    access.reset();
    expect(await access.restore({ savedKey: 'second-key' })).toBe(true);
    complete(denied());
    expect(await late).toBe(false);
    expect(access.authenticated.value).toBe(true);
    expect(access.error.value).toBe('');
    expect(fetcher.mock.calls[2]![0]).toBe('https://second.example/api/profile-session');
  } finally {
    activeServer.value = original;
  }
});
