import { ref } from 'vue';

// Password and API bearer stay in memory; the server remembers this browser
// using a revocable HttpOnly cookie. Profile keys are separately transferable.
export function useAccess() {
  const session = ref(''), profileKey = ref(''), storageNote = ref(''), member=ref<{name:string;avatar?:string}|null>(null);
  try { profileKey.value = localStorage.getItem('nimbo-profile-key') || ''; } catch { /* storage is optional */ }
  const authenticated = ref(false), authenticating = ref(false), error = ref('');
  const restoring = ref(false), restoreFailed = ref(false);
  let renewal:Promise<boolean>|undefined;
  let controller: AbortController | undefined;
  let generation = 0;
  async function restore(background=false) {
    const current=++generation;if(!background)restoring.value=true;error.value='';
    controller=new AbortController();const requestController=controller;
    const timeout=setTimeout(()=>requestController.abort(),10000);
    try {
      const response=await fetch('/api/session',{signal:requestController.signal});
      if(current!==generation)return false;
      if(response.status===401){restoreFailed.value=false;return false;}
      if(!response.ok)throw new Error();
      const result=await response.json();if(current!==generation)return false;
      session.value=result.session;member.value=result.member;authenticated.value=true;restoreFailed.value=false;return true;
    } catch {if(current===generation){restoreFailed.value=true;error.value='Сервер пока не отвечает. Повторяем подключение — заново входить не нужно.';}return false;}
    finally {clearTimeout(timeout);if(current===generation){restoring.value=false;controller=undefined;}}
  }
  function renew(){if(!renewal)renewal=restore(true).finally(()=>{renewal=undefined;});return renewal;}
  async function signOut() {
    try {
      const response=await fetch('/api/session',{method:'DELETE',signal:AbortSignal.timeout(10000)});
      if(!response.ok)throw new Error();reset();return true;
    } catch {error.value='Не удалось выйти на сервере. Проверьте соединение и повторите выход.';return false;}
  }
  async function signIn(name: string, password: string, fromProfile=false) {
    if (authenticating.value) return false;
    const current = ++generation;
    controller = new AbortController();
    const requestController = controller;
    const timeout = setTimeout(() => requestController.abort(), 10000);
    authenticating.value = true; error.value = '';
    try {
      const response = await fetch(fromProfile?'/api/profile-session':'/api/auth', { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(fromProfile?{profileKey:profileKey.value}:{ name: name.trim(), password, ...(profileKey.value ? { profileKey: profileKey.value } : {}) }), signal: requestController.signal });
      const result = await response.json();
      if (current !== generation) return false;
      if (!response.ok) { error.value = result.error || 'Не удалось войти.'; return false; }
      session.value = result.session || '';
      member.value=result.member||null;
      if (result.profileKey) {
        profileKey.value = result.profileKey;
        try { localStorage.setItem('nimbo-profile-key', result.profileKey); } catch { storageNote.value = 'Браузер не сохранил профиль. Скачайте файл в разделе «Мой профиль».'; }
      }
      authenticated.value = true;
      return true;
    } catch {
      if (current === generation) error.value = 'Не удалось связаться с сервером. Попробуйте ещё раз.';
      return false;
    } finally {
      clearTimeout(timeout);
      if (current === generation) { authenticating.value = false; controller = undefined; }
    }
  }
  function reset() { ++generation; controller?.abort(); controller = undefined; authenticated.value = false; session.value = ''; member.value=null; restoring.value=false; restoreFailed.value=false; authenticating.value = false; error.value = ''; }
  return { session, profileKey, member, storageNote, authenticated, authenticating, restoring, restoreFailed, error, signIn, restore, renew, signOut, reset };
}
