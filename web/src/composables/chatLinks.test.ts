import { describe, it, expect } from 'vitest';
import { linkParts } from './chatLinks';
describe('chat links', () => {
  it('keeps punctuation and balanced URL parentheses', () => {
    expect(linkParts('Вот (https://example.org/a(b)). www.example.org!')).toEqual([
      { text: 'Вот (' },
      { text: 'https://example.org/a(b)', href: 'https://example.org/a(b)' },
      { text: '). ' },
      { text: 'www.example.org', href: 'https://www.example.org/' },
      { text: '!' },
    ]);
  });
  it('never links script URLs, credentials, or an embedded URL', () => {
    for (const s of [
      'javascript:alert(1)',
      'https://user:pass@example.org',
      'xhttps://example.org',
      'a@www.example.org',
    ])
      expect(linkParts(s)).toEqual([{ text: s }]);
  });
  it('preserves HTML-looking text without generating markup', () => {
    const s = '<img src=x onerror=alert(1)> https://example.org';
    expect(linkParts(s)[0]?.text).toBe('<img src=x onerror=alert(1)> ');
  });
});
