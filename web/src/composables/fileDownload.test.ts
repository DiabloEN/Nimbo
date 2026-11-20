import { afterEach, expect, it, vi } from 'vitest';
import { activeServer, clientOrigin } from './serverConnection';
import { useWorkspace, type Attachment } from './useWorkspace';

afterEach(() => {
  activeServer.value = clientOrigin;
  vi.unstubAllGlobals();
  vi.useRealTimers();
});

function documentStub() {
  const anchor = { href: '', download: '', hidden: false, click: vi.fn(), remove: vi.fn() };
  const appendChild = vi.fn();
  vi.stubGlobal('document', { createElement: () => anchor, body: { appendChild } });
  return { anchor, appendChild };
}
const file: Attachment = {
  id: 'safe-file',
  name: 'file.bin',
  size: 1500 * 1024 ** 2,
  scanned: false,
};

it('starts a first-party browser download during the click without fetching or buffering', async () => {
  activeServer.value = clientOrigin;
  const { anchor, appendChild } = documentStub();
  const fetch = vi.fn();
  vi.stubGlobal('fetch', fetch);
  const ws = useWorkspace({ value: 'private-session' });
  const pending = ws.downloadFile(file);
  expect(anchor.click).toHaveBeenCalledOnce();
  expect(appendChild).toHaveBeenCalledWith(anchor);
  expect(anchor.remove).toHaveBeenCalledOnce();
  expect(anchor.href).toBe('/api/session/media/safe-file?download=1');
  expect(anchor.href).not.toContain('private-session');
  expect(anchor.download).toBe(file.name);
  expect(fetch).not.toHaveBeenCalled();
  await pending;
  ws.stop();
});

it('retains compatibility with old servers that do not support download grants', async () => {
  activeServer.value = 'https://server.example';
  vi.useFakeTimers();
  const { anchor } = documentStub();
  const blob = new Blob(['data']);
  const fetch = vi.fn(async (url: string) =>
    url.endsWith('/download')
      ? { ok: false, status: 404, body: { cancel: vi.fn() } }
      : { ok: true, status: 200, blob: async () => blob },
  );
  vi.stubGlobal('fetch', fetch);
  const createObjectURL = vi.fn(() => 'blob:temporary');
  const revokeObjectURL = vi.fn();
  const ws = useWorkspace({ value: 'private-session' });
  const create = vi.spyOn(URL, 'createObjectURL').mockImplementation(createObjectURL);
  const revoke = vi.spyOn(URL, 'revokeObjectURL').mockImplementation(revokeObjectURL);
  try {
    await ws.downloadFile(file);
    expect(fetch).toHaveBeenCalledWith(
      'https://server.example/api/files/safe-file',
      expect.objectContaining({
        credentials: 'omit',
        headers: expect.objectContaining({ Authorization: 'Bearer private-session' }),
      }),
    );
    expect(createObjectURL).toHaveBeenCalledWith(blob);
    expect(anchor.href).toBe('blob:temporary');
    expect(anchor.click).toHaveBeenCalledOnce();
    await vi.advanceTimersByTimeAsync(1000);
    expect(revokeObjectURL).toHaveBeenCalledWith('blob:temporary');
  } finally {
    create.mockRestore();
    revoke.mockRestore();
    ws.stop();
  }
});

it('starts a cross-server download with only a scoped ticket and never buffers file bytes', async () => {
  activeServer.value = 'https://server.example';
  const { anchor } = documentStub();
  const ticket = 'd'.repeat(43);
  const fetch = vi.fn(async () => ({ ok: true, status: 200, json: async () => ({ ticket }) }));
  vi.stubGlobal('fetch', fetch);
  const ws = useWorkspace({ value: 'private-session' });
  await ws.downloadFile(file);
  expect(fetch).toHaveBeenCalledTimes(1);
  expect(fetch).toHaveBeenCalledWith(
    'https://server.example/api/files/safe-file/download',
    expect.objectContaining({ method: 'POST', credentials: 'omit' }),
  );
  expect(anchor.href).toBe('https://server.example/api/download/' + ticket);
  expect(anchor.href).not.toContain('private-session');
  expect(anchor.click).toHaveBeenCalledOnce();
  expect(anchor.remove).toHaveBeenCalledOnce();
  ws.stop();
});

it('does not start a download after the workspace changes while authorization is pending', async () => {
  activeServer.value = 'https://server.example';
  const { anchor } = documentStub();
  let reply!: (value: unknown) => void;
  vi.stubGlobal(
    'fetch',
    () =>
      new Promise((resolve) => {
        reply = resolve;
      }),
  );
  const ws = useWorkspace({ value: 'private-session' });
  const pending = ws.downloadFile(file);
  ws.stop();
  reply({ ok: true, status: 200, json: async () => ({ ticket: 'd'.repeat(43) }) });
  await expect(pending).rejects.toThrow('Запрос отменён');
  expect(anchor.click).not.toHaveBeenCalled();
});

it('does not send another server session to the old server after renewal', async () => {
  activeServer.value = 'https://server.example';
  const { anchor } = documentStub();
  const session = { value: 'private-session' };
  const fetch = vi.fn(async () => ({ ok: false, status: 401 }));
  vi.stubGlobal('fetch', fetch);
  const ws = useWorkspace(session, async () => {
    activeServer.value = 'https://other.example';
    session.value = 'other-private-session';
    return true;
  });
  await expect(ws.downloadFile(file)).rejects.toThrow('Запрос отменён');
  expect(fetch).toHaveBeenCalledTimes(1);
  expect(anchor.click).not.toHaveBeenCalled();
  ws.stop();
});
