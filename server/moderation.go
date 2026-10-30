package main

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"slices"
	"strings"
	"time"
)

func isBlocked(m *member) bool {
	return m != nil && m.Blocked && (m.BlockedUntil == 0 || m.BlockedUntil > time.Now().UnixMilli())
}

// Only mute existing publications of the requested source. Never remotely
// enable devices, change publish permissions, or touch another member's media.
func (p *presenceService) muteMemberMedia(ctx context.Context, id, action string) error {
	if p == nil {
		return errors.New("media service unavailable")
	}
	sources := map[string]string{"mute_mic": "MICROPHONE", "mute_camera": "CAMERA", "mute_screen": "SCREEN_SHARE"}
	source, ok := sources[action]
	if !ok {
		return errors.New("unknown media action")
	}
	var rooms struct {
		Rooms []struct {
			Name string `json:"name"`
		} `json:"rooms"`
	}
	if err := p.rpc(ctx, "ListRooms", "", &rooms); err != nil {
		return err
	}
	for _, room := range rooms.Rooms {
		if p.store != nil && !p.store.voiceAllowed(room.Name) {
			continue
		}
		var people struct {
			Participants []struct {
				Identity string `json:"identity"`
				Tracks   []struct {
					SID    string          `json:"sid"`
					Source json.RawMessage `json:"source"`
					Muted  bool            `json:"muted"`
				} `json:"tracks"`
			} `json:"participants"`
		}
		if err := p.rpc(ctx, "ListParticipants", room.Name, &people); err != nil {
			return err
		}
		for _, person := range people.Participants {
			if !strings.HasPrefix(person.Identity, id+".") {
				continue
			}
			for _, track := range person.Tracks {
				kind := strings.Trim(string(track.Source), `"`)
				if n := map[string]string{"1": "CAMERA", "2": "MICROPHONE", "3": "SCREEN_SHARE", "4": "SCREEN_SHARE_AUDIO"}[kind]; n != "" {
					kind = n
				}
				if track.SID == "" || track.Muted || (kind != source && !(source == "SCREEN_SHARE" && kind == "SCREEN_SHARE_AUDIO")) {
					continue
				}
				var result map[string]any
				if err := p.rpcBody(ctx, "MutePublishedTrack", room.Name, &result, map[string]any{"room": room.Name, "identity": person.Identity, "track_sid": track.SID, "muted": true}); err != nil {
					return err
				}
			}
		}
	}
	return nil
}

func (p *presenceService) disconnectMember(ctx context.Context, id string) error {
	if p == nil {
		return nil
	}
	var list struct {
		Rooms []struct {
			Name string `json:"name"`
		} `json:"rooms"`
	}
	if err := p.rpc(ctx, "ListRooms", "", &list); err != nil {
		return err
	}
	for _, room := range list.Rooms {
		var people struct {
			Participants []voicePerson `json:"participants"`
		}
		if err := p.rpc(ctx, "ListParticipants", room.Name, &people); err != nil {
			return err
		}
		for _, person := range people.Participants {
			if strings.HasPrefix(person.Identity, id+".") {
				var result map[string]any
				if err := p.rpc(ctx, "RemoveParticipant", room.Name, &result, person.Identity); err != nil {
					return err
				}
			}
		}
	}
	return nil
}
func (s *workspaceStore) registerModeration(handle func(string, func(http.ResponseWriter, *http.Request, string)), presence *presenceService) {
	handle("DELETE /api/membership", func(w http.ResponseWriter, r *http.Request, actor string) {
		err := s.update(func(data *workspaceData) error {
			m := findMember(data, actor)
			if m == nil {
				return fail(401, "Войдите в Nimbo заново")
			}
			if m.Role == "owner" {
				return fail(403, "Сначала передайте владение сервером другому участнику")
			}
			data.Members = slices.DeleteFunc(data.Members, func(m member) bool { return m.ID == actor })
			for key, entry := range data.Sessions {
				if entry.MemberID == actor {
					delete(data.Sessions, key)
				}
			}
			for channel, messages := range data.Messages {
				for i := range messages {
					messages[i].MentionUnread = slices.DeleteFunc(messages[i].MentionUnread, func(id string) bool { return id == actor })
					for emoji, people := range messages[i].Reactions {
						people = slices.DeleteFunc(people, func(id string) bool { return id == actor })
						if len(people) == 0 {
							delete(messages[i].Reactions, emoji)
						} else {
							messages[i].Reactions[emoji] = people
						}
					}
				}
				data.Messages[channel] = messages
			}
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		// The poller also evicts departed identities, including late reconnects.
		if presence != nil {
			ctx, cancel := context.WithTimeout(r.Context(), 8*time.Second)
			defer cancel()
			_ = presence.disconnectMember(ctx, actor)
		}
		jsonReply(w, 200, map[string]bool{"ok": true})
	})
	handle("POST /api/members/{id}/moderate", func(w http.ResponseWriter, r *http.Request, actor string) {
		var req struct {
			Action  string `json:"action"`
			Seconds int64  `json:"seconds"`
		}
		if !decodeRequest(w, r, &req) {
			return
		}
		target := r.PathValue("id")
		err := s.update(func(data *workspaceData) error {
			permission := "kick_members"
			switch req.Action {
			case "block", "unblock":
				permission = "ban_members"
			case "mute", "unmute":
				permission = "manage_chat"
			}
			if err := requirePermission(data, actor, permission); err != nil {
				return err
			}
			m := findMember(data, target)
			if m == nil {
				return fail(404, "Участник не найден")
			}
			if protectedTarget(data, actor, target) {
				return fail(403, "Владельца нельзя отключить или заблокировать")
			}
			switch req.Action {
			case "block":
				if req.Seconds < 0 || req.Seconds > 31536000 || (req.Seconds > 0 && req.Seconds < 60) {
					return fail(400, "Срок блокировки: от минуты до года или навсегда")
				}
				m.Blocked = true
				m.BlockedUntil = 0
				if req.Seconds > 0 {
					m.BlockedUntil = time.Now().Add(time.Duration(req.Seconds) * time.Second).UnixMilli()
				}
			case "unblock":
				m.Blocked = false
				m.BlockedUntil = 0
			case "mute":
				if req.Seconds < 60 || req.Seconds > 604800 {
					return fail(400, "Срок: от минуты до недели")
				}
				m.ChatMutedUntil = time.Now().Add(time.Duration(req.Seconds) * time.Second).UnixMilli()
			case "unmute":
				m.ChatMutedUntil = 0
			case "kick", "mute_mic", "mute_camera", "mute_screen":
			default:
				return fail(400, "Неизвестное действие")
			}
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		if req.Action == "mute_mic" || req.Action == "mute_camera" || req.Action == "mute_screen" {
			ctx, cancel := context.WithTimeout(r.Context(), 8*time.Second)
			defer cancel()
			if err := presence.muteMemberMedia(ctx, target, req.Action); err != nil {
				reject(w, 502, "Не удалось выключить медиа участника. Повторите действие.")
				return
			}
		}
		if req.Action == "kick" || req.Action == "block" {
			ctx, cancel := context.WithTimeout(r.Context(), 8*time.Second)
			defer cancel()
			if err := presence.disconnectMember(ctx, target); err != nil {
				message := "Не удалось отключить от голосовой. Повторите действие."
				if req.Action == "block" {
					message = "Профиль заблокирован, но завершить текущий звонок не удалось. Повторите отключение."
				}
				reject(w, 502, message)
				return
			}
		}
		jsonReply(w, 200, map[string]bool{"ok": true})
	})
}
