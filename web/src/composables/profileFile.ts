import { parseBindings, type Bindings } from './useHotkeys';
export type SavedServer = {
  url: string;
  name: string;
  displayName: string;
  profileKey: string;
  pinned?: boolean;
  customName?: boolean;
  role?: 'owner' | 'admin' | 'member';
};
export type ProfileFile = {
  format: 'nimbo-profile';
  version: 1;
  name: string;
  avatar: string;
  servers: SavedServer[];
  hotkeys?: Bindings;
  cameraEffects?: { enabled: boolean; strength: number };
};
export const blankProfile = (): ProfileFile => ({
  format: 'nimbo-profile',
  version: 1,
  name: '',
  avatar: '',
  servers: [],
});
const keyPattern = /^[A-Za-z0-9_-]{43}$/;
export function newServerKey() {
  return btoa(String.fromCharCode(...crypto.getRandomValues(new Uint8Array(32))))
    .replaceAll('+', '-')
    .replaceAll('/', '_')
    .replace(/=+$/, '');
}
export function serverAddress(value: string): string {
  let url: URL;
  try {
    url = new URL(
      /^[a-z][a-z\d+.-]*:\/\//i.test(value.trim()) ? value.trim() : 'https://' + value.trim(),
    );
  } catch {
    throw new Error('Введите адрес сервера, например https://nimbo.example.com.');
  }
  if (
    (url.protocol !== 'https:' &&
      !(url.protocol === 'http:' && ['localhost', '127.0.0.1', '[::1]'].includes(url.hostname))) ||
    url.username ||
    url.password ||
    url.pathname !== '/' ||
    url.search
  )
    throw new Error('Нужен HTTPS-адрес сервера без пути, логина и пароля.');
  return url.origin;
}
export function validAvatar(value: unknown): value is string {
  return (
    typeof value === 'string' &&
    (value === '' ||
      (value.length <= 49152 && /^data:image\/(jpeg|png);base64,[A-Za-z0-9+/]+=*$/.test(value)))
  );
}
function name(value: unknown, max = 40): value is string {
  return (
    typeof value === 'string' &&
    value.trim().length > 0 &&
    [...value].length <= max &&
    !/[\u0000-\u001f\u007f]/.test(value)
  );
}
export function parseProfile(text: string): ProfileFile {
  if (text.length > 100000) throw new Error('Файл профиля слишком большой.');
  let p: any;
  try {
    p = JSON.parse(text);
  } catch {
    throw new Error('Не удалось прочитать файл профиля.');
  }
  if (
    !p ||
    p.format !== 'nimbo-profile' ||
    p.version !== 1 ||
    !name(p.name) ||
    !validAvatar(p.avatar) ||
    !Array.isArray(p.servers) ||
    p.servers.length > 64
  )
    throw new Error('Это не поддерживаемый файл профиля Nimbo.');
  const seen = new Set<string>();
  const servers = p.servers.map((s: any) => {
    let url: URL;
    try {
      url = new URL(s.url);
    } catch {
      throw new Error('Некорректный адрес сервера.');
    }
    if (
      (url.protocol !== 'https:' &&
        !(
          url.protocol === 'http:' && ['localhost', '127.0.0.1', '[::1]'].includes(url.hostname)
        )) ||
      url.username ||
      url.password ||
      (url.href !== url.origin + '/' && url.href !== url.origin) ||
      !name(s.name, 60) ||
      !name(s.displayName) ||
      !keyPattern.test(s.profileKey) ||
      seen.has(url.origin)
    )
      throw new Error('Некорректная запись сервера в профиле.');
    seen.add(url.origin);
    return {
      url: url.origin,
      name: s.name,
      displayName: s.displayName,
      profileKey: s.profileKey,
      ...(s.pinned === true ? { pinned: true } : {}),
      ...(s.customName === true ? { customName: true } : {}),
      ...(['owner', 'admin', 'member'].includes(s.role) ? { role: s.role } : {}),
    };
  });
  return {
    format: 'nimbo-profile',
    version: 1,
    name: p.name.trim(),
    avatar: p.avatar,
    servers,
    ...(p.cameraEffects &&
    typeof p.cameraEffects.enabled === 'boolean' &&
    Number.isFinite(p.cameraEffects.strength)
      ? {
          cameraEffects: {
            enabled: p.cameraEffects.enabled,
            strength: Math.max(1, Math.min(60, Math.round(p.cameraEffects.strength))),
          },
        }
      : {}),
    ...(p.hotkeys === undefined ? {} : { hotkeys: parseBindings(p.hotkeys) }),
  };
}
export function readProfile(): ProfileFile {
  try {
    return parseProfile(localStorage.getItem('nimbo-profile-file') || '');
  } catch {
    return blankProfile();
  }
}
export function storeProfile(profile: ProfileFile) {
  localStorage.setItem('nimbo-profile-file', JSON.stringify(profile));
}
export async function avatarFromFile(file: File) {
  if (!['image/jpeg', 'image/png', 'image/webp'].includes(file.type) || file.size > 8 * 1024 * 1024)
    throw new Error('Выберите PNG, JPEG или WebP до 8 МБ.');
  const bitmap = await createImageBitmap(file);
  try {
    const canvas = document.createElement('canvas');
    canvas.width = 192;
    canvas.height = 192;
    const ctx = canvas.getContext('2d')!;
    const side = Math.min(bitmap.width, bitmap.height);
    ctx.fillStyle = '#202020';
    ctx.fillRect(0, 0, 192, 192);
    ctx.drawImage(
      bitmap,
      (bitmap.width - side) / 2,
      (bitmap.height - side) / 2,
      side,
      side,
      0,
      0,
      192,
      192,
    );
    const result = canvas.toDataURL('image/jpeg', 0.78);
    if (!validAvatar(result))
      throw new Error('Изображение слишком сложное. Выберите другой аватар.');
    return result;
  } finally {
    bitmap.close();
  }
}
export function downloadProfile(profile: ProfileFile) {
  const url = URL.createObjectURL(
    new Blob([JSON.stringify(profile, null, 2)], { type: 'application/json' }),
  );
  const link = document.createElement('a');
  link.href = url;
  link.download = 'profile.nimbo';
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function serverProfileUrl(server: SavedServer) {
  return server.url + '/#/connect/' + encodeURIComponent(server.profileKey);
}

// A backup may add servers to the same identity, but must never replace a
// different credential already saved for an address or roll back preferences.
export function mergeProfileServers(current: ProfileFile, incoming: ProfileFile): ProfileFile {
  const servers = [...current.servers];
  for (const entry of incoming.servers)
    if (!servers.some((s) => s.url === entry.url)) servers.push(entry);
  return parseProfile(JSON.stringify({ ...current, servers }));
}
