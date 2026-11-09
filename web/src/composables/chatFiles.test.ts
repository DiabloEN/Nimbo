import { describe, it, expect, vi } from 'vitest';
import { fileSelectionError, preventFileNavigation, MAX_CHAT_FILE_BYTES } from './chatFiles';
const file = (size: number) => ({ size, name: 'example.txt' }) as File;
describe('chat file drafts', () => {
  it('accepts one nonempty file up to 20 MiB', () => {
    expect(fileSelectionError([file(1)])).toBe('');
    expect(fileSelectionError([file(MAX_CHAT_FILE_BYTES)])).toBe('');
    expect(fileSelectionError([file(MAX_CHAT_FILE_BYTES + 1)])).not.toBe('');
    expect(fileSelectionError([file(0)])).not.toBe('');
  });
  it('does not silently select just one of multiple dropped files', () => {
    expect(fileSelectionError([file(1), file(2)])).toBe('');
    expect(fileSelectionError(Array.from({ length: 11 }, () => file(1)))).not.toBe('');
    expect(fileSelectionError([file(MAX_CHAT_FILE_BYTES), file(1)])).not.toBe('');
    expect(fileSelectionError([file(1), file(0)])).not.toBe('');
  });
  it('prevents browser navigation for file drops', () => {
    const preventDefault = vi.fn();
    expect(
      preventFileNavigation({
        dataTransfer: { types: ['Files'] },
        preventDefault,
      } as unknown as DragEvent),
    ).toBe(true);
    expect(preventDefault).toHaveBeenCalledOnce();
  });
  it('preserves text and URL dragging', () => {
    const preventDefault = vi.fn();
    expect(
      preventFileNavigation({
        dataTransfer: { types: ['text/plain', 'text/uri-list'] },
        preventDefault,
      } as unknown as DragEvent),
    ).toBe(false);
    expect(preventDefault).not.toHaveBeenCalled();
  });
});
