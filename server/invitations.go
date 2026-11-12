package main

import (
	"crypto/hmac"
	"net/http"
	"slices"
	"strings"
	"time"
)

type invitation struct {
	ID        string `json:"id"`
	Code      string `json:"code"`
	Label     string `json:"label,omitempty"`
	CreatedAt int64  `json:"createdAt"`
	ExpiresAt int64  `json:"expiresAt,omitempty"`
	SingleUse bool   `json:"singleUse"`
}

func invitationForCode(d *workspaceData, code string) int {
	now := time.Now().UnixMilli()
	for i, item := range d.Invitations {
		if (item.ExpiresAt == 0 || item.ExpiresAt > now) && hmac.Equal([]byte(item.Code), []byte(code)) {
			return i
		}
	}
	return -1
}

func activeInvitations(d *workspaceData) []invitation {
	result := []invitation{}
	now := time.Now().UnixMilli()
	for _, item := range d.Invitations {
		if item.ExpiresAt == 0 || item.ExpiresAt > now {
			result = append(result, item)
		}
	}
	return result
}
func (s *workspaceStore) ensureInvitations(legacy string) error {
	s.mu.Lock()
	ready := s.data.InvitationsReady
	s.mu.Unlock()
	if ready {
		return nil
	}
	return s.update(func(d *workspaceData) error {
		if d.InvitationsReady {
			return nil
		}
		d.InvitationsReady = true
		if legacy != "" {
			d.Invitations = append(d.Invitations, invitation{ID: randomKey(), Code: legacy, CreatedAt: time.Now().UnixMilli()})
		}
		return nil
	})
}
func (s *workspaceStore) registerInvitations(handle func(string, func(http.ResponseWriter, *http.Request, string))) {
	handle("GET /api/invitations", func(w http.ResponseWriter, r *http.Request, actor string) {
		s.mu.Lock()
		err := requirePermission(&s.data, actor, "manage_server")
		items := []invitation{}
		if err == nil {
			items = activeInvitations(&s.data)
		}
		s.mu.Unlock()
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, items)
	})
	handle("POST /api/invitations", func(w http.ResponseWriter, r *http.Request, actor string) {
		var req struct {
			SingleUse bool   `json:"singleUse"`
			Seconds   int64  `json:"seconds"`
			Label     string `json:"label"`
		}
		if !decodeRequest(w, r, &req) {
			return
		}
		var item invitation
		err := s.update(func(d *workspaceData) error {
			if err := requirePermission(d, actor, "manage_server"); err != nil {
				return err
			}
			if req.Seconds < 0 || req.Seconds > 31536000 || (req.Seconds > 0 && req.Seconds < 60) {
				return fail(400, "Срок приглашения: от минуты до года или без срока")
			}
			label := strings.TrimSpace(req.Label)
			if label != "" && !validLabel(label, 80) {
				return fail(400, "Подпись должна быть одной строкой до 80 символов")
			}
			d.Invitations = activeInvitations(d)
			if len(d.Invitations) >= 100 {
				return fail(409, "Можно создать не больше 100 активных приглашений")
			}
			item = invitation{ID: randomKey(), Code: randomKey(), Label: label, SingleUse: req.SingleUse, CreatedAt: time.Now().UnixMilli()}
			if req.Seconds > 0 {
				item.ExpiresAt = time.Now().Add(time.Duration(req.Seconds) * time.Second).UnixMilli()
			}
			d.Invitations = append(d.Invitations, item)
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 201, item)
	})
	handle("DELETE /api/invitations/{id}", func(w http.ResponseWriter, r *http.Request, actor string) {
		err := s.update(func(d *workspaceData) error {
			if err := requirePermission(d, actor, "manage_server"); err != nil {
				return err
			}
			d.Invitations = slices.DeleteFunc(activeInvitations(d), func(item invitation) bool { return item.ID == r.PathValue("id") })
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, map[string]bool{"ok": true})
	})
}
