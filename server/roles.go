package main

import (
	"net/http"
	"slices"
	"strings"
)

var allPermissions = []string{"manage_server", "manage_channels", "manage_chat", "kick_members", "ban_members", "manage_roles"}

type workspaceRole struct {
	ID          string   `json:"id"`
	Name        string   `json:"name"`
	Permissions []string `json:"permissions"`
}

func rolesFor(d *workspaceData) []workspaceRole {
	name := d.OwnerRoleName
	if name == "" {
		name = "Владелец"
	}
	result := []workspaceRole{{"owner", name, append([]string{}, allPermissions...)}}
	definitions := d.Roles
	if definitions == nil {
		definitions = []workspaceRole{{"member", "Участник", []string{}}, {"moderator", "Модератор", []string{"manage_chat", "kick_members"}}, {"admin", "Администратор", append([]string{}, allPermissions...)}}
	}
	for _, role := range definitions {
		if role.ID != "owner" {
			result = append(result, role)
		}
	}
	return result
}
func roleFor(d *workspaceData, id string) *workspaceRole {
	for _, r := range rolesFor(d) {
		if r.ID == id {
			return &r
		}
	}
	return nil
}
func memberRoles(m *member) []string {
	if m == nil {
		return []string{}
	}
	if m.RoleIDs != nil {
		return append([]string{}, m.RoleIDs...)
	}
	if m.Role == "admin" {
		return []string{"admin"}
	}
	return []string{}
}
func permissionsFor(d *workspaceData, m *member) []string {
	if m == nil || isBlocked(m) {
		return []string{}
	}
	if m.Role == "owner" {
		return append([]string{}, allPermissions...)
	}
	permissions := []string{}
	for _, id := range memberRoles(m) {
		if id == "owner" {
			continue
		}
		if r := roleFor(d, id); r != nil {
			for _, p := range r.Permissions {
				if !slices.Contains(permissions, p) {
					permissions = append(permissions, p)
				}
			}
		}
	}
	return permissions
}
func hasPermission(d *workspaceData, id, p string) bool {
	return slices.Contains(permissionsFor(d, findMember(d, id)), p)
}
func requirePermission(d *workspaceData, id, p string) error {
	if !hasPermission(d, id, p) {
		return fail(403, "Недостаточно прав")
	}
	return nil
}
func canGrant(d *workspaceData, actor string, permissions []string) bool {
	for _, p := range permissions {
		if !hasPermission(d, actor, p) {
			return false
		}
	}
	return true
}
func protectedTarget(d *workspaceData, actor, target string) bool {
	m := findMember(d, target)
	a := findMember(d, actor)
	return m == nil || a == nil || m.Role == "owner" || actor == target || (a.Role != "owner" && hasPermission(d, target, "manage_roles"))
}
func validateRole(name string, permissions []string) error {
	if !validLabel(name, 40) || len(permissions) > len(allPermissions) {
		return fail(400, "Проверьте название и разрешения роли")
	}
	seen := map[string]bool{}
	for _, p := range permissions {
		if !slices.Contains(allPermissions, p) || seen[p] {
			return fail(400, "Неизвестное или повторное разрешение")
		}
		seen[p] = true
	}
	return nil
}
func (s *workspaceStore) registerRoles(handle func(string, func(http.ResponseWriter, *http.Request, string))) {
	change := func(w http.ResponseWriter, r *http.Request, actor string) {
		var req struct {
			Name        string   `json:"name"`
			Permissions []string `json:"permissions"`
		}
		if r.Method != "DELETE" {
			if !decodeRequest(w, r, &req) {
				return
			}
			req.Name = strings.TrimSpace(req.Name)
			if err := validateRole(req.Name, req.Permissions); err != nil {
				report(w, err)
				return
			}
		}
		var result workspaceRole
		err := s.update(func(d *workspaceData) error {
			if err := requirePermission(d, actor, "manage_roles"); err != nil {
				return err
			}
			id := r.PathValue("id")
			if id == "owner" {
				if findMember(d, actor).Role != "owner" {
					return fail(403, "Название роли владельца может менять только владелец")
				}
				if r.Method != "PATCH" || len(req.Permissions) > 0 {
					return fail(400, "Роль владельца можно только переименовать; её права закреплены")
				}
				d.OwnerRoleName = req.Name
				result = workspaceRole{"owner", req.Name, append([]string{}, allPermissions...)}
				return nil
			}
			if r.Method == "POST" {
				if len(rolesFor(d))-1 >= 64 {
					return fail(400, "Не более 64 ролей")
				}
				id = randomKey()
			} else {
				old := roleFor(d, id)
				if old == nil {
					return fail(404, "Роль не найдена")
				}
				if id == "member" && (r.Method == "DELETE" || len(req.Permissions) > 0) {
					return fail(400, "Базовую роль можно только переименовать")
				}
				if id == "admin" && r.Method == "DELETE" {
					return fail(400, "Базовую роль администратора нельзя удалить")
				}
				if findMember(d, actor).Role != "owner" && (slices.Contains(old.Permissions, "manage_roles") || slices.Contains(memberRoles(findMember(d, actor)), id) || !canGrant(d, actor, old.Permissions)) {
					return fail(403, "Нельзя изменять свою роль или роль с более широкими правами")
				}
			}
			if r.Method != "DELETE" && !canGrant(d, actor, req.Permissions) {
				return fail(403, "Нельзя выдать разрешения, которых у вас нет")
			}
			// Materialize legacy assignments before deleting a definition.
			for i := range d.Members {
				if d.Members[i].RoleIDs == nil {
					d.Members[i].RoleIDs = memberRoles(&d.Members[i])
				}
			}
			definitions := slices.DeleteFunc(append([]workspaceRole{}, rolesFor(d)...), func(role workspaceRole) bool { return role.ID == "owner" })
			if r.Method == "DELETE" {
				d.Roles = slices.DeleteFunc(definitions, func(role workspaceRole) bool { return role.ID == id })
				for i := range d.Channels {
					if p := d.Channels[i].Posting; p != nil {
						p.RoleIDs = slices.DeleteFunc(p.RoleIDs, func(role string) bool { return role == id })
					}
				}
				for i := range d.Members {
					d.Members[i].RoleIDs = slices.DeleteFunc(d.Members[i].RoleIDs, func(role string) bool { return role == id })
				}
				return nil
			}
			result = workspaceRole{id, req.Name, append([]string{}, req.Permissions...)}
			found := false
			for i := range definitions {
				if definitions[i].ID == id {
					definitions[i] = result
					found = true
				}
			}
			if !found {
				definitions = append(definitions, result)
			}
			d.Roles = definitions
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, result)
	}
	handle("POST /api/roles", change)
	handle("PATCH /api/roles/{id}", change)
	handle("DELETE /api/roles/{id}", change)
	handle("PUT /api/members/{id}/roles", func(w http.ResponseWriter, r *http.Request, actor string) {
		var req struct {
			RoleIDs []string `json:"roleIds"`
		}
		if !decodeRequest(w, r, &req) {
			return
		}
		err := s.update(func(d *workspaceData) error {
			if err := requirePermission(d, actor, "manage_roles"); err != nil {
				return err
			}
			target := r.PathValue("id")
			m := findMember(d, target)
			if m == nil {
				return fail(404, "Участник не найден")
			}
			if protectedTarget(d, actor, target) {
				return fail(403, "Роли этого участника может изменить только владелец; свои роли менять нельзя")
			}
			if len(req.RoleIDs) > 64 {
				return fail(400, "Слишком много ролей")
			}
			seen := map[string]bool{}
			for _, id := range req.RoleIDs {
				role := roleFor(d, id)
				if role == nil || id == "member" || id == "owner" || seen[id] {
					return fail(400, "Неизвестная или повторная роль")
				}
				seen[id] = true
				if !canGrant(d, actor, role.Permissions) {
					return fail(403, "Нельзя выдать разрешения, которых у вас нет")
				}
			}
			for _, id := range memberRoles(m) {
				if role := roleFor(d, id); role != nil && !canGrant(d, actor, role.Permissions) {
					return fail(403, "Недостаточно прав для изменения ролей участника")
				}
			}
			m.RoleIDs = append([]string{}, req.RoleIDs...)
			m.Role = "member"
			if seen["admin"] {
				m.Role = "admin"
			}
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, map[string]bool{"ok": true})
	})
}
