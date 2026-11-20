import { afterEach, expect, it, vi } from 'vitest';
import { createVoicePresenceWatch } from './voicePresenceWatch';
import { serverFetch } from './serverConnection';
vi.mock('./serverConnection', () => ({ serverFetch: vi.fn() }));
const connection = {
  origin: 'https://server.test',
  room: 'a',
  identity: 'member.one',
  session: 'session',
};
afterEach(() => {
  vi.clearAllMocks();
  vi.useRealTimers();
});
it('keeps one stream per connection and aborts it when the call ends', () => {
  vi.mocked(serverFetch).mockImplementation(() => new Promise(() => {}));
  const watch = createVoicePresenceWatch();
  watch.update(connection);
  watch.update({ ...connection });
  expect(serverFetch).toHaveBeenCalledTimes(1);
  const signal = vi.mocked(serverFetch).mock.calls[0][2]?.signal;
  watch.stop();
  expect(signal?.aborted).toBe(true);
});
it('replaces the room stream without retaining the old request', () => {
  vi.mocked(serverFetch).mockImplementation(() => new Promise(() => {}));
  const watch = createVoicePresenceWatch();
  watch.update(connection);
  const signal = vi.mocked(serverFetch).mock.calls[0][2]?.signal;
  watch.update({ ...connection, room: 'b' });
  expect(signal?.aborted).toBe(true);
  expect(serverFetch).toHaveBeenCalledTimes(2);
  watch.stop();
});
it('retries an interrupted stream and stops retrying on disposal', async () => {
  vi.useFakeTimers();
  vi.mocked(serverFetch).mockRejectedValue(new Error('network'));
  const watch = createVoicePresenceWatch();
  watch.update(connection);
  await vi.advanceTimersByTimeAsync(250);
  expect(serverFetch).toHaveBeenCalledTimes(2);
  watch.stop();
  await vi.advanceTimersByTimeAsync(1000);
  expect(serverFetch).toHaveBeenCalledTimes(2);
});
it('does not repeatedly call an older server without support', async () => {
  vi.useFakeTimers();
  vi.mocked(serverFetch).mockResolvedValue(new Response('', { status: 404 }));
  const watch = createVoicePresenceWatch();
  watch.update(connection);
  await vi.advanceTimersByTimeAsync(1000);
  expect(serverFetch).toHaveBeenCalledTimes(1);
  watch.stop();
});
