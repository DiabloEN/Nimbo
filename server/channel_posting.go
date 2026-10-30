package main

import "slices"

type channelPostingPolicy struct {
	Mode    string   `json:"mode"`
	RoleIDs []string `json:"roleIds"`
}

func setChannelPosting(d *workspaceData, ch *channel, policy *channelPostingPolicy) error {
	if policy == nil {
		return nil
	}
	if ch.Kind != "text" {
		return fail(400, "Настройка записи доступна только текстовым каналам")
	}
	if policy.Mode == "everyone" {
		ch.Posting = nil
		return nil
	}
	if policy.Mode != "roles" || len(policy.RoleIDs) > 64 {
		return fail(400, "Выберите, кто может писать в канал")
	}
	ids := []string{}
	for _, id := range policy.RoleIDs {
		if roleFor(d, id) == nil || slices.Contains(ids, id) {
			return fail(400, "Неизвестная или повторная роль")
		}
		ids = append(ids, id)
	}
	ch.Posting = &channelPostingPolicy{Mode: "roles", RoleIDs: ids}
	return nil
}

func requireChannelWriter(d *workspaceData, ch *channel, actor string) error {
	m := findMember(d, actor)
	if m == nil || isBlocked(m) {
		return fail(403, "Нет доступа к каналу")
	}
	if m.Role == "owner" || ch.Posting == nil || ch.Posting.Mode == "everyone" {
		return nil
	}
	if ch.Posting.Mode == "roles" {
		for _, id := range ch.Posting.RoleIDs {
			if id != "owner" && roleFor(d, id) != nil && ((id == "member" && m.Role == "member") || slices.Contains(memberRoles(m), id)) {
				return nil
			}
		}
	}
	return fail(403, "В этом канале могут писать только выбранные роли")
}
