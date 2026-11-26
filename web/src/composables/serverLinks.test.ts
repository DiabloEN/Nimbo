import { expect, it } from 'vitest';
import { invitationLink, openedServerLink, readServerLink, returnLink } from './serverLinks';

const server = 'https://chat.example:8443';
const code = 'AbCdEf0123456789_-abcd';

it('copies one URL and resolves the server, including its port', () => {
  const url = invitationLink(server, code);
  expect(url).toBe(server + '/#/invite/' + code);
  expect(readServerLink(url)).toEqual({ origin: server, code });
  expect(returnLink(server)).toBe(server);
  expect(readServerLink(returnLink(server), { plainOrigin: true })).toEqual({
    origin: server,
    code: '',
  });
});
it('selects the pasted server explicitly but keeps ordinary chat links eligible for previews', () => {
  expect(readServerLink(server + '/', { plainOrigin: true })).toEqual({ origin: server, code: '' });
  expect(readServerLink(server)).toBeUndefined();
  expect(readServerLink('сайт ' + server, { plainOrigin: true })).toBeUndefined();
  expect(readServerLink(server + '/?room=123', { plainOrigin: true })).toBeUndefined();
});
it('selects the root for a new visit and restores the saved server on internal routes, reload and history', () => {
  const back = { origin: server, code: '' };
  expect(openedServerLink(server, 'navigate')).toEqual(back);
  expect(openedServerLink(server)).toEqual(back);
  expect(openedServerLink(server, 'reload')).toBeUndefined();
  expect(openedServerLink(server, 'back_forward')).toBeUndefined();
  expect(openedServerLink(server + '/#/room', 'navigate')).toBeUndefined();
  for (const type of ['navigate', 'reload', 'back_forward'] as const) {
    expect(openedServerLink(server + '/#/server', type)).toEqual(back);
    expect(openedServerLink(invitationLink(server, code), type)).toEqual({ origin: server, code });
  }
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
