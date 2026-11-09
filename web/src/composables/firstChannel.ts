import type { Channel, ChannelSection } from './useWorkspace';
export function firstSidebarChannel(
  channels: Channel[],
  order: string[],
  sections: ChannelSection[],
) {
  for (const id of order) {
    const section = sections.find((s) => s.id === id);
    if (!section) continue;
    const channel = channels.find((c) => (c.section || c.kind) === id && c.kind === section.kind);
    if (channel) return channel;
  }
  return channels[0];
}
