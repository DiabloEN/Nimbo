import { it, expect } from 'vitest';
import { encryptProfile, decryptProfile, inspectEncryptedProfile } from './encryptedProfile';
import type { ProfileFile } from './profileFile';
const profile: ProfileFile = {
  format: 'nimbo-profile',
  version: 1,
  name: 'Private name',
  avatar: '',
  servers: [
    {
      url: 'https://private.example.com',
      name: 'Secret server',
      displayName: 'Alias',
      profileKey: 'q'.repeat(43),
    },
  ],
};
const password = 'a long test passphrase';
it('encrypts all profile fields, round trips and randomizes each export', async () => {
  const first = await encryptProfile(profile, password),
    second = await encryptProfile(profile, password);
  for (const value of [
    profile.name,
    profile.servers[0]!.url,
    profile.servers[0]!.profileKey,
    password,
  ])
    expect(first).not.toContain(value);
  expect(first).not.toBe(second);
  expect(await decryptProfile(first, password)).toEqual(profile);
});
it('rejects wrong password and authenticated ciphertext tampering', async () => {
  const text = await encryptProfile(profile, password);
  await expect(decryptProfile(text, 'a wrong test passphrase')).rejects.toThrow('Неверный пароль');
  const p = JSON.parse(text);
  p.data = (p.data[0] === 'A' ? 'B' : 'A') + p.data.slice(1);
  await expect(decryptProfile(JSON.stringify(p), password)).rejects.toThrow('повреждён');
});
it('rejects unsupported KDF parameters and large files before any derivation', async () => {
  const text = await encryptProfile(profile, password),
    p = JSON.parse(text);
  p.iterations = 999999999;
  expect(() => inspectEncryptedProfile(JSON.stringify(p))).toThrow();
  expect(() => inspectEncryptedProfile(' '.repeat(300001))).toThrow();
  await expect(encryptProfile(profile, 'short')).rejects.toThrow('от 6 до 256');
});
