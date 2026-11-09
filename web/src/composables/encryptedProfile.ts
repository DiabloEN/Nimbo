import { parseProfile, type ProfileFile } from './profileFile';

// Portable files only. Passwords never leave the browser or enter localStorage.
const iterations = 600000;
const aad = new TextEncoder().encode('nimbo-encrypted-profile:v1:PBKDF2-SHA256:600000:AES-256-GCM');
const message = 'Неверный пароль или файл повреждён.';
const encode = (bytes: Uint8Array) => {
  let text = '';
  for (const byte of bytes) text += String.fromCharCode(byte);
  return btoa(text);
};
function decode(value: unknown, max: number) {
  if (
    typeof value !== 'string' ||
    value.length > Math.ceil(max / 3) * 4 ||
    !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value)
  )
    throw new Error('Некорректный зашифрованный файл.');
  const bytes = Uint8Array.from(atob(value), (c) => c.charCodeAt(0));
  if (bytes.length > max) throw new Error('Файл слишком большой.');
  return bytes;
}
export function inspectEncryptedProfile(text: string) {
  if (text.length > 300000) throw new Error('Файл профиля слишком большой.');
  let p: any;
  try {
    p = JSON.parse(text);
  } catch {
    throw new Error('Не удалось прочитать файл профиля.');
  }
  if (
    p?.format !== 'nimbo-encrypted-profile' ||
    p.version !== 1 ||
    p.kdf !== 'PBKDF2-SHA256' ||
    p.iterations !== iterations ||
    p.cipher !== 'AES-256-GCM'
  )
    throw new Error('Не поддерживаемый формат зашифрованного профиля.');
  const salt = decode(p.salt, 16),
    iv = decode(p.iv, 12),
    data = decode(p.data, 200016);
  if (salt.length !== 16 || iv.length !== 12 || data.length < 17)
    throw new Error('Некорректный зашифрованный файл.');
  return { salt, iv, data };
}
async function key(password: string, salt: Uint8Array<ArrayBuffer>) {
  if (!crypto.subtle) throw new Error('Для шифрования нужен HTTPS и современный браузер.');
  if (password.length < 6 || password.length > 256)
    throw new Error('Пароль файла должен содержать от 6 до 256 символов.');
  const material = await crypto.subtle.importKey(
    'raw',
    new TextEncoder().encode(password),
    'PBKDF2',
    false,
    ['deriveKey'],
  );
  return crypto.subtle.deriveKey(
    { name: 'PBKDF2', salt, iterations, hash: 'SHA-256' },
    material,
    { name: 'AES-GCM', length: 256 },
    false,
    ['encrypt', 'decrypt'],
  );
}
export async function encryptProfile(profile: ProfileFile, password: string) {
  const clean = parseProfile(JSON.stringify(profile)),
    plain = new TextEncoder().encode(JSON.stringify(clean));
  if (plain.byteLength > 200000) throw new Error('Файл профиля слишком большой.');
  const salt = crypto.getRandomValues(new Uint8Array(16)),
    iv = crypto.getRandomValues(new Uint8Array(12));
  const data = await crypto.subtle.encrypt(
    { name: 'AES-GCM', iv, additionalData: aad, tagLength: 128 },
    await key(password, salt),
    plain,
  );
  return JSON.stringify({
    format: 'nimbo-encrypted-profile',
    version: 1,
    kdf: 'PBKDF2-SHA256',
    iterations,
    cipher: 'AES-256-GCM',
    salt: encode(salt),
    iv: encode(iv),
    data: encode(new Uint8Array(data)),
  });
}
export async function decryptProfile(text: string, password: string) {
  const { salt, iv, data } = inspectEncryptedProfile(text);
  const secret = await key(password, salt);
  let plain: ArrayBuffer;
  try {
    plain = await crypto.subtle.decrypt(
      { name: 'AES-GCM', iv, additionalData: aad, tagLength: 128 },
      secret,
      data,
    );
  } catch {
    throw new Error(message);
  }
  try {
    return parseProfile(new TextDecoder('utf-8', { fatal: true }).decode(plain));
  } catch {
    throw new Error(message);
  }
}
export async function downloadEncryptedProfile(profile: ProfileFile, password: string) {
  const text = await encryptProfile(profile, password),
    url = URL.createObjectURL(new Blob([text], { type: 'application/octet-stream' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = 'profile.nimbo';
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
