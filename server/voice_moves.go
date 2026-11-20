package main

import (
	"context"
	"encoding/json"
	"net/http"
	"strings"
	"time"
)

// Commands belong to a connection, never to every session of a profile. They
// expire in memory and are revalidated at delivery if roles/channels changed.
type voiceMove struct {
	ID    string    `json:"id"`
	Room  string    `json:"room"`
	Name  string    `json:"name"`
	Actor string    `json:"-"`
	Until time.Time `json:"-"`
}
type voiceControl struct {
	Room  string
	Until time.Time
	Move  *voiceMove
}

func (p *presenceService) pruneControls() {
	if p.controls == nil {
		p.controls = map[string]*voiceControl{}
	}
	for id, c := range p.controls {
		if time.Now().After(c.Until) {
			delete(p.controls, id)
		}
	}
}

func voiceMoveAllowed(d *workspaceData, actor, target, from, to string) bool {
	a, b := findChannel(d, from), findChannel(d, to)
	m := findMember(d, target)
	return a != nil && b != nil && a.Kind == "voice" && b.Kind == "voice" && from != to &&
		hasPermission(d, actor, "move_members") && m != nil && !isBlocked(m)
}

func (p *presenceService) voiceControlPoll(w http.ResponseWriter, r *http.Request, id string) {
	var req struct {
		Identity string `json:"identity"`
		Room     string `json:"room"`
	}
	if !decodeRequest(w, r, &req) {
		return
	}
	if !strings.HasPrefix(req.Identity, id+".") || len(req.Identity) > 150 || !p.store.voiceAllowed(req.Room) {
		reject(w, 400, "Некорректное подключение")
		return
	}
	p.store.mu.Lock()
	defer p.store.mu.Unlock()
	p.mu.Lock()
	defer p.mu.Unlock()
	p.pruneControls()
	// Register only identities observed on the media server. A caller cannot
	// fill the queue by inventing connection suffixes.
	present := false
	for _, person := range p.rooms[req.Room].People {
		if person.Identity == req.Identity {
			present = true
			break
		}
	}
	if !present || time.Since(p.checked) > 12*time.Second {
		jsonReply(w, 200, map[string]any{})
		return
	}
	c := p.controls[req.Identity]
	if c == nil || c.Room != req.Room {
		if len(p.controls) >= 2000 {
			reject(w, 429, "Слишком много подключений")
			return
		}
		c = &voiceControl{Room: req.Room}
		p.controls[req.Identity] = c
	}
	c.Until = time.Now().Add(15 * time.Second)
	if m := c.Move; m != nil {
		if time.Now().After(m.Until) || !voiceMoveAllowed(&p.store.data, m.Actor, id, req.Room, m.Room) {
			c.Move = nil
		} else {
			jsonReply(w, 200, map[string]any{"move": m})
			return
		}
	}
	jsonReply(w, 200, map[string]any{})
}

func (p *presenceService) moveVoice(w http.ResponseWriter, r *http.Request, actor string) {
	var req struct {
		Identity string `json:"identity"`
		From     string `json:"from"`
		To       string `json:"to"`
	}
	if !decodeRequest(w, r, &req) {
		return
	}
	target, suffix, ok := strings.Cut(req.Identity, ".")
	if !ok || suffix == "" || len(req.Identity) > 150 {
		reject(w, 400, "Некорректное подключение")
		return
	}
	p.store.mu.Lock()
	allowed := voiceMoveAllowed(&p.store.data, actor, target, req.From, req.To)
	p.store.mu.Unlock()
	if !allowed {
		reject(w, 403, "Нет права переносить этого участника в выбранный канал")
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 4*time.Second)
	defer cancel()
	var live struct {
		Participants []voicePerson `json:"participants"`
	}
	if err := p.rpc(ctx, "ListParticipants", req.From, &live); err != nil {
		reject(w, 502, "Не удалось проверить подключение участника")
		return
	}
	present := false
	for _, person := range live.Participants {
		if person.Identity == req.Identity {
			present = true
			break
		}
	}
	if !present {
		reject(w, 409, "Участник уже вышел или сменил канал")
		return
	}
	locked := true
	p.store.mu.Lock()
	defer func() {
		if locked {
			p.store.mu.Unlock()
		}
	}()
	if !voiceMoveAllowed(&p.store.data, actor, target, req.From, req.To) {
		reject(w, 403, "Право переноса или канал изменились")
		return
	}
	p.mu.Lock()
	defer func() {
		if locked {
			p.mu.Unlock()
		}
	}()
	p.pruneControls()
	c := p.controls[req.Identity]
	if c == nil || c.Room != req.From {
		reject(w, 409, "Клиент участника пока не поддерживает перенос. Попросите его обновить страницу")
		return
	}
	if c.Move != nil && time.Now().Before(c.Move.Until) {
		reject(w, 409, "Предыдущий перенос ещё выполняется")
		return
	}
	c.Move = &voiceMove{ID: randomKey(), Room: req.To, Name: findChannel(&p.store.data, req.To).Name, Actor: actor, Until: time.Now().Add(12 * time.Second)}
	// The recipient validates the command through authenticated HTTP; no client
	// data packet can authorize a move. This only eliminates the polling delay.
	p.mu.Unlock()
	p.store.mu.Unlock()
	locked = false
	payload, _ := json.Marshal(map[string]bool{"check": true})
	var sent map[string]any
	_ = p.rpcBody(ctx, "SendData", req.From, &sent, map[string]any{"room": req.From, "data": payload, "kind": "RELIABLE", "destination_identities": []string{req.Identity}, "topic": "nimbo.voice-move"})
	jsonReply(w, 202, map[string]bool{"accepted": true})
}

// Admit an authorized move with the same connection identity. Stable identity
// prevents the old/new rooms from rendering two copies during roster delivery.
func (p *presenceService) joinVoiceMove(w http.ResponseWriter, r *http.Request, id string) {
	var req struct {
		Identity string `json:"identity"`
		MoveID   string `json:"moveId"`
	}
	if !decodeRequest(w, r, &req) {
		return
	}
	if !strings.HasPrefix(req.Identity, id+".") {
		reject(w, 403, "Чужое подключение")
		return
	}
	p.store.mu.Lock()
	defer p.store.mu.Unlock()
	p.mu.Lock()
	defer p.mu.Unlock()
	c := p.controls[req.Identity]
	if c == nil || c.Move == nil || c.Move.ID != req.MoveID || time.Now().After(c.Move.Until) || !voiceMoveAllowed(&p.store.data, c.Move.Actor, id, c.Room, c.Move.Room) {
		reject(w, 409, "Перенос отменён или устарел")
		return
	}
	annotations := r.URL.Query().Get("annotations") == "1"
	token, err := makeToken(p.c, req.Identity, findMember(&p.store.data, id).Name, c.Move.Room, time.Now(), annotations)
	if err != nil {
		reject(w, 500, "Не удалось подготовить перенос")
		return
	}
	// Returning to an earlier room reuses the connection identity. Its old
	// departure retry must not evict the newly authorized admission.
	if hint, ok := p.hints[req.Identity]; ok && hint.Room == c.Move.Room {
		delete(p.hints, req.Identity)
	}
	jsonReply(w, 200, map[string]any{"url": p.c.MediaURL, "token": token, "identity": req.Identity, "expiresIn": 600, "annotations": annotations})
}
