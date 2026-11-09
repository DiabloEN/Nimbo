<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount } from 'vue';
const id = location.hash.slice('#/enroll/'.length),
  note = ref('Подключаем профиль к серверу…');
let started = false,
  timer: ReturnType<typeof setInterval> | undefined;
async function receive(event: MessageEvent) {
  if (
    started ||
    !window.opener ||
    event.source !== window.opener ||
    event.data?.type !== 'nimbo-enroll-request' ||
    event.data.id !== id
  )
    return;
  let origin: URL;
  try {
    origin = new URL(event.origin);
  } catch {
    return;
  }
  if (
    origin.protocol !== 'https:' &&
    !(origin.protocol === 'http:' && ['localhost', '127.0.0.1', '[::1]'].includes(origin.hostname))
  )
    return;
  const { name, password, profileKey } = event.data;
  if (
    typeof name !== 'string' ||
    typeof password !== 'string' ||
    typeof profileKey !== 'string' ||
    name.length > 160 ||
    password.length > 256 ||
    !/^[A-Za-z0-9_-]{43}$/.test(profileKey)
  )
    return;
  started = true;
  clearInterval(timer);
  try {
    const response = await fetch('/api/enroll', {
      method: 'POST',
      credentials: 'omit',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name, password, profileKey }),
      signal: AbortSignal.timeout(20000),
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'Не удалось присоединиться.');
    window.opener.postMessage({ type: 'nimbo-enroll-result', id, result }, event.origin);
    note.value = 'Сервер добавлен. Можно закрыть это окно.';
  } catch (e) {
    note.value = (e as Error).message;
    window.opener?.postMessage(
      { type: 'nimbo-enroll-result', id, error: note.value },
      event.origin,
    );
  }
}
onMounted(() => {
  window.addEventListener('message', receive);
  if (!window.opener) {
    note.value = 'Добавьте сервер из своего профиля Nimbo.';
    return;
  }
  const ready = () => window.opener?.postMessage({ type: 'nimbo-enroll-ready', id }, '*');
  ready();
  timer = setInterval(ready, 250);
});
onBeforeUnmount(() => {
  clearInterval(timer);
  window.removeEventListener('message', receive);
});
</script>
<template>
  <main class="auth-page">
    <section class="auth-form">
      <span class="brand">nimbo.</span>
      <h1>Добавление сервера</h1>
      <p class="field-help" role="status">{{ note }}</p>
    </section>
  </main>
</template>
