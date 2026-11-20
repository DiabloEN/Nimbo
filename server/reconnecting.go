package main

import (
	"context"
	"net/http"
	"strings"
	"time"
)

type voiceHint struct {
	Room   string
	Person voicePerson
	Left   bool
	Until  time.Time
}

func (p *presenceService) voiceState(w http.ResponseWriter, r *http.Request, id string) {
	var req struct {
		Room     string `json:"room"`
		Identity string `json:"identity"`
		State    string `json:"state"`
	}
	if !decodeRequest(w, r, &req) {
		return
	}
	if !strings.HasPrefix(req.Identity, id+".") || len(req.Identity) > 150 || !p.store.voiceAllowed(req.Room) || (req.State != "reconnecting" && req.State != "left" && req.State != "active") {
		reject(w, 400, "Некорректное состояние звонка")
		return
	}
	p.store.mu.Lock()
	m := findMember(&p.store.data, id)
	name := m.Name
	p.store.mu.Unlock()
	p.mu.Lock()

	if p.hints == nil {
		p.hints = map[string]voiceHint{}
	}
	for key, h := range p.hints {
		if time.Now().After(h.Until) {
			delete(p.hints, key)
		}
	}
	if req.State == "active" {
		delete(p.hints, req.Identity)
	} else {
		if len(p.hints) >= 2000 {
			p.mu.Unlock()
			reject(w, 429, "Слишком много переподключений")
			return
		}
		person := voicePerson{Identity: req.Identity, Name: name, JoinedAt: unixSeconds(time.Now().Unix()), Reconnecting: true}
		for _, old := range p.rooms[req.Room].People {
			if old.Identity == req.Identity {
				person = old
				person.Reconnecting = true
			}
		}
		ttl := 4 * time.Second
		if req.State == "left" {
			ttl = 30 * time.Second
		}
		p.hints[req.Identity] = voiceHint{Room: req.Room, Person: person, Left: req.State == "left", Until: time.Now().Add(ttl)}
	}
	p.mu.Unlock()
	if req.State == "left" {
		// Explicit departure must reach connected SDK peers too, not just hide a
		// cached row. Scope removal to this connection; another tab stays in call.
		ctx, cancel := context.WithTimeout(r.Context(), 3*time.Second)
		defer cancel()
		var removed map[string]any
		_ = p.rpc(ctx, "RemoveParticipant", req.Room, &removed, req.Identity)
	}
	jsonReply(w, 200, map[string]bool{"ok": true})
}

func (p *presenceService) withHints() map[string]voicePresence {
	result := map[string]voicePresence{}
	for id, room := range p.rooms {
		room.People = append([]voicePerson{}, room.People...)
		result[id] = room
	}
	for key, h := range p.hints {
		if time.Now().After(h.Until) {
			delete(p.hints, key)
			continue
		}
		room := result[h.Room]
		people := []voicePerson{}
		for _, person := range room.People {
			if person.Identity == key {
				if h.Left {
					continue
				}
				person.Reconnecting = true
			}
			people = append(people, person)
		}
		// A navigation hint must never resurrect a participant absent from LiveKit.
		room.People = people
		if room.Since == 0 {
			room.Since = int64(h.Person.JoinedAt) * 1000
		}
		result[h.Room] = room
	}
	latest := map[string]string{}
	joined := map[string]unixSeconds{}
	for id, room := range result {
		for _, person := range room.People {
			desired := ""
			if c := p.controls[person.Identity]; c != nil && c.Move != nil && time.Now().Before(c.Move.Until) {
				desired = c.Move.Room
			}
			if latest[person.Identity] == desired && desired != "" {
				continue
			}
			if desired == id {
				latest[person.Identity] = id
				joined[person.Identity] = person.JoinedAt
				continue
			}
			if previous, ok := latest[person.Identity]; !ok || person.JoinedAt > joined[person.Identity] || (person.JoinedAt == joined[person.Identity] && id > previous) {
				latest[person.Identity] = id
				joined[person.Identity] = person.JoinedAt
			}
		}
	}
	for id, room := range result {
		people := room.People[:0]
		for _, person := range room.People {
			if latest[person.Identity] == id {
				people = append(people, person)
			}
		}
		room.People = people
		result[id] = room
	}
	for id, room := range result {
		if len(room.People) == 0 {
			delete(result, id)
		}
	}
	return result
}
