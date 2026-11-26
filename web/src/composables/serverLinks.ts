import { serverAddress } from './profileFile';

export function invitationLink(origin: string, code: string) {
  return serverAddress(origin) + '/#/invite/' + encodeURIComponent(code);
}

export function returnLink(origin: string) {
  return serverAddress(origin);
}

export function readServerLink(text: string, { plainOrigin = false } = {}) {
  // Old clients copied a multi-line message containing the link as well as the code.
  for (const token of text.trim().split(/\s+/)) {
    if (!/^https?:\/\//i.test(token)) continue;
    try {
      const url = new URL(token);
      const origin = serverAddress(url.href);
      if (plainOrigin && token === text.trim() && url.href === origin + '/')
        return { origin, code: '' };
      if (url.hash === '#/server') return { origin, code: '' };
      const route = /^#\/(?:i|invite)\/(.+)$/.exec(url.hash);
      if (!route) continue;
      const code = decodeURIComponent(route[1]);
      if (code.length >= 16 && code.length <= 256 && !/[\u0000-\u001f\u007f]/.test(code))
        return { origin, code };
    } catch {
      // Ordinary addresses and invitation codes are handled by their existing forms.
    }
  }
}

export function openedServerLink(href: string, navigationType?: NavigationTimingType) {
  // An unmarked root names a server on a new visit. Reload/history restore may
  // belong to a multi-server tab and should keep its selected workspace.
  return readServerLink(href, { plainOrigin: !navigationType || navigationType === 'navigate' });
}
