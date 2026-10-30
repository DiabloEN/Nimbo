package main

import (
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
	defer p.mu.Unlock()
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
	for id, room := range result {
		if len(room.People) == 0 {
			delete(result, id)
		}
	}
	return result
}
