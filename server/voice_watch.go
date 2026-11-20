package main

import (
	"context"
	"io"
	"net/http"
	"strings"
	"time"
)

type voiceWatchKey struct{ Room, Identity string }
type voiceWatch struct {
	Member   string
	Replaced chan struct{}
}

const voiceWatchGrace = 2 * time.Second

// A streaming response survives backgrounding, but closes with the browser tab
// even when its host skips beforeunload/pagehide. Authentication stays in headers.
func (p *presenceService) watchVoice(w http.ResponseWriter, r *http.Request, memberID string) {
	var req struct {
		Room     string `json:"room"`
		Identity string `json:"identity"`
	}
	if !decodeRequest(w, r, &req) {
		return
	}
	if !strings.HasPrefix(req.Identity, memberID+".") || len(req.Identity) > 150 || !p.store.voiceAllowed(req.Room) {
		reject(w, 400, "Некорректное подключение")
		return
	}
	key := voiceWatchKey{req.Room, req.Identity}
	watch := &voiceWatch{Member: memberID, Replaced: make(chan struct{})}
	p.mu.Lock()
	if p.watches == nil {
		p.watches = map[voiceWatchKey]*voiceWatch{}
	}
	old := p.watches[key]
	count := 0
	for _, v := range p.watches {
		if v.Member == memberID {
			count++
		}
	}
	if old == nil && (len(p.watches) >= 2000 || count >= 16) {
		p.mu.Unlock()
		reject(w, 429, "Слишком много подключений")
		return
	}
	if old != nil {
		close(old.Replaced)
	}
	p.watches[key] = watch
	if hint, ok := p.hints[key.Identity]; ok && hint.Room == key.Room {
		delete(p.hints, key.Identity)
	}
	p.mu.Unlock()
	defer func() {
		// Allow a transient HTTP reconnect without interrupting healthy media.
		go func() {
			timer := time.NewTimer(voiceWatchGrace)
			defer timer.Stop()
			select {
			case <-watch.Replaced:
				return
			case <-timer.C:
			}
			p.mu.Lock()
			if p.watches[key] != watch {
				p.mu.Unlock()
				return
			}
			delete(p.watches, key)
			if p.hints == nil {
				p.hints = map[string]voiceHint{}
			}
			p.hints[key.Identity] = voiceHint{Room: key.Room, Left: true, Until: time.Now().Add(30 * time.Second)}
			p.mu.Unlock()
			ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			defer cancel()
			var removed map[string]any
			_ = p.rpc(ctx, "RemoveParticipant", key.Room, &removed, key.Identity)
		}()
	}()
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("X-Accel-Buffering", "no")
	control := http.NewResponseController(w)
	send := func() bool {
		_ = control.SetWriteDeadline(time.Now().Add(5 * time.Second))
		_, err := io.WriteString(w, ": connected\n\n")
		return err == nil && control.Flush() == nil
	}
	if !send() {
		return
	}
	ticker := time.NewTicker(10 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-r.Context().Done():
			return
		case <-watch.Replaced:
			return
		case <-ticker.C:
			if p.store.sessionMember(p.c, r) != memberID || !p.store.voiceAllowed(req.Room) || !send() {
				return
			}
		}
	}
}
