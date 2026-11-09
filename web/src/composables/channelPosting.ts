import type { Channel, Member } from './useWorkspace';
export function canWriteChannel(channel: Channel | undefined, member: Member | undefined): boolean {
  if (!channel || channel.kind !== 'text' || !member || member.blocked) return false;
  if (member.role === 'owner' || !channel.posting || channel.posting.mode === 'everyone')
    return true;
  const roles = member.roleIds ?? (member.role === 'admin' ? ['admin'] : []);
  return (
    channel.posting.mode === 'roles' &&
    channel.posting.roleIds.some(
      (id) =>
        id !== 'owner' && ((id === 'member' && member.role === 'member') || roles.includes(id)),
    )
  );
}
