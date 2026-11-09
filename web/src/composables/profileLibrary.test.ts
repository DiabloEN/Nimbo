import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { useProfileLibrary } from './profileLibrary';
import { blankProfile, serverProfileUrl, type ProfileFile } from './profileFile';
const dev = {
  url: 'https://dev.example.com',
  name: 'Dev',
  displayName: 'Tester',
  profileKey: 'a'.repeat(43),
  role: 'owner' as const,
};
const prod = {
  ...dev,
  url: 'https://prod.example.com:7443',
  name: 'Prod',
  profileKey: 'b'.repeat(43),
};
const profile = (servers = [dev]): ProfileFile => ({ ...blankProfile(), name: 'Tester', servers });
beforeEach(() => {
  const values = new Map<string, string>();
  vi.stubGlobal('localStorage', {
    getItem: (k: string) => values.get(k) ?? null,
    setItem: (k: string, v: string) => values.set(k, v),
  });
});
afterEach(() => vi.unstubAllGlobals());
it('restores a newly enrolled server into the same profile without consuming another invite', () => {
  const library = useProfileLibrary();
  const id = library.add(profile());
  library.select(id);
  expect(library.add(profile([dev, prod]))).toBe(id);
  expect(library.entries.value).toHaveLength(1);
  expect(library.current()!.servers).toEqual([dev, prod]);
  expect(useProfileLibrary().current()!.servers).toEqual([dev, prod]);
  library.add(profile());
  expect(library.current()!.servers).toHaveLength(2);
});
it('never replaces another identity key or local preferences when importing a backup', () => {
  const library = useProfileLibrary();
  const local = { ...profile([dev, prod]), name: 'Local name' };
  const id = library.add(local);
  library.select(id);
  library.add(profile([dev, { ...prod, profileKey: 'c'.repeat(43) }]));
  expect(library.current()).toEqual(local);
});
it('does not merge unrelated people just because they have the same name', () => {
  const library = useProfileLibrary();
  library.add(profile());
  library.add(profile([{ ...dev, profileKey: 'd'.repeat(43) }]));
  expect(library.entries.value).toHaveLength(2);
});
it('a server card carries saved access even when it is the current origin', () => {
  vi.stubGlobal('location', { origin: prod.url });
  expect(serverProfileUrl(prod)).toBe(prod.url + '/#/connect/' + prod.profileKey);
});
