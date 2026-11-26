import { expect, it } from 'vitest';
import { invitationLink, readServerLink, returnLink } from './serverLinks';

const server = 'https://chat.example:8443';
const code = 'AbCdEf0123456789_-abcd';

it('copies one URL and resolves the server, including its port', () => {
  const url = invitationLink(server, code);
  expect(url).toBe(server + '/#/invite/' + code);
  expect(readServerLink(url)).toEqual({ origin: server, code });
  expect(readServerLink(returnLink(server))).toEqual({ origin: server, code: '' });
});
it('reads existing multiline invitations and encoded legacy codes', () => {
  const old = code + '/+';
  const url = invitationLink(server, old);
  expect(
    readServerLink('Приглашение в Nimbo\nСсылка для входа: ' + url + '\nАдрес сервера: ' + server),
  ).toEqual({ origin: server, code: old });
});
it('does not mistake credentials or malformed addresses for invitations', () => {
  for (const raw of [
    server + '/#/connect/' + code,
    server + '/#/invite/%GG',
    server + '/#/invite/short',
    server + '/private/#/invite/' + code,
    'https://user@chat.example/#/invite/' + code,
    'http://chat.example/#/invite/' + code,
  ]) {
    expect(readServerLink(raw)).toBeUndefined();
  }
});
