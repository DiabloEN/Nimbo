import { describe, it, expect } from 'vitest';
import { firstSidebarChannel } from './firstChannel';
import type { Channel, ChannelSection } from './useWorkspace';
const channels: Channel[] = [
  { id: 'game', name: 'Game', kind: 'voice', description: '' },
  { id: 'general', name: 'General', kind: 'text', description: '' },
  { id: 'custom', name: 'Custom', kind: 'text', section: 'friends', description: '' },
];
const sections: ChannelSection[] = [
  { id: 'voice', name: 'Voice', kind: 'voice' },
  { id: 'text', name: 'Text', kind: 'text' },
  { id: 'friends', name: 'Friends', kind: 'text' },
  { id: 'empty', name: 'Empty', kind: 'voice' },
];
describe('first visible sidebar channel', () => {
  it('respects section order rather than raw channel storage order', () => {
    expect(firstSidebarChannel(channels, ['text', 'voice', 'friends'], sections)?.id).toBe(
      'general',
    );
  });
  it('supports custom sections and skips empty or missing sections', () => {
    expect(
      firstSidebarChannel(channels, ['missing', 'empty', 'friends', 'text'], sections)?.id,
    ).toBe('custom');
  });
  it('opens voice if it is genuinely first and handles no channels', () => {
    expect(firstSidebarChannel(channels, ['voice', 'text'], sections)?.id).toBe('game');
    expect(firstSidebarChannel([], ['text'], sections)).toBeUndefined();
  });
});
