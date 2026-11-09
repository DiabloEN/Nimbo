export type Enrollment = {
  serverName: string;
  member: { name: string; role?: 'owner' | 'admin' | 'member' };
  profileKey: string;
};
export function enrollServer(
  url: string,
  name: string,
  password: string,
  profileKey: string,
): Promise<Enrollment> {
  const id = crypto.randomUUID();
  const popup = window.open(url + '/#/enroll/' + id, '_blank', 'popup,width=560,height=380');
  if (!popup)
    return Promise.reject(new Error('Разрешите открытие окна сервера и повторите добавление.'));
  return new Promise((resolve, reject) => {
    let sent = false;
    const stop = () => {
      clearInterval(poll);
      clearTimeout(timeout);
      window.removeEventListener('message', receive);
      popup.close();
    };
    const receive = (event: MessageEvent) => {
      if (event.source !== popup || event.origin !== url || event.data?.id !== id) return;
      if (event.data.type === 'nimbo-enroll-ready' && !sent) {
        sent = true;
        popup.postMessage({ type: 'nimbo-enroll-request', id, name, password, profileKey }, url);
      }
      if (event.data.type === 'nimbo-enroll-result') {
        stop();
        if (event.data.error) reject(new Error(event.data.error));
        else resolve(event.data.result);
      }
    };
    window.addEventListener('message', receive);
    const poll = setInterval(() => {
      if (popup.closed) {
        stop();
        reject(new Error('Окно сервера закрыто. Повторите добавление.'));
      }
    }, 500);
    const timeout = setTimeout(() => {
      stop();
      reject(new Error('Сервер не ответил. Проверьте адрес и повторите добавление.'));
    }, 30000);
  });
}
