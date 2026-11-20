import { serverFetch } from './serverConnection';

type Connection = { origin: string; room: string; identity: string; session: string };

export function createVoicePresenceWatch() {
  let current: Connection | undefined;
  let controller: AbortController | undefined;
  let retry: ReturnType<typeof setTimeout> | undefined;

  function stop() {
    current = undefined;
    clearTimeout(retry);
    controller?.abort();
    controller = undefined;
  }
  async function connect(connection: Connection) {
    const abort = new AbortController();
    controller = abort;
    let supported = true;
    try {
      const response = await serverFetch(connection.origin, '/api/voice-watch', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          Authorization: 'Bearer ' + connection.session,
        },
        body: JSON.stringify({ room: connection.room, identity: connection.identity }),
        signal: abort.signal,
      });
      if (!response.ok || !response.body) {
        // Older servers do not have this optional endpoint. Media still works.
        supported = ![400, 401, 403, 404, 405, 429].includes(response.status);
        await response.body?.cancel();
        return;
      }
      const reader = response.body.getReader();
      try {
        while (!(await reader.read()).done) {
          /* bounded heartbeat, no accumulation */
        }
      } finally {
        reader.releaseLock();
      }
    } catch {
      /* retry an interrupted stream while the call still owns it */
    } finally {
      if (current === connection && !abort.signal.aborted && supported)
        retry = setTimeout(() => void connect(connection), 250);
    }
  }
  function update(connection: Connection) {
    if (
      current &&
      Object.entries(connection).every(
        ([key, value]) => current?.[key as keyof Connection] === value,
      )
    )
      return;
    stop();
    current = connection;
    void connect(connection);
  }
  return { update, stop };
}
