package main

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func TestVoiceWatchCloseReplacementAndScope(t *testing.T) {
	s, _ := openWorkspace("", "")
	removed := make(chan voiceWatchKey, 8)
	media := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var body struct {
			Room     string
			Identity string
		}
		json.NewDecoder(r.Body).Decode(&body)
		removed <- voiceWatchKey{body.Room, body.Identity}
		jsonReply(w, 200, map[string]any{})
	}))
	defer media.Close()
	p := newPresence(testConfig(), s, media.URL)
	handler := newWorkspaceHandler(testConfig(), s, p)
	member, token, _ := appLogin(t, handler, "Watcher", "")
	server := httptest.NewServer(handler)
	defer server.Close()
	open := func(identity string) *http.Response {
		b, _ := json.Marshal(map[string]string{"room": "lounge", "identity": identity})
		req, _ := http.NewRequest("POST", server.URL+"/api/voice-watch", bytes.NewReader(b))
		req.Header.Set("Authorization", "Bearer "+token)
		req.Header.Set("Content-Type", "application/json")
		r, e := http.DefaultClient.Do(req)
		if e != nil {
			t.Fatal(e)
		}
		return r
	}
	foreign := open("foreign.connection")
	foreign.Body.Close()
	if foreign.StatusCode != 400 {
		t.Fatal(foreign.StatusCode)
	}
	id := member.ID + ".one"
	first := open(id)
	if first.StatusCode != 200 {
		t.Fatal(first.StatusCode)
	}
	first.Body.Close()
	second := open(id)
	select {
	case <-removed:
		t.Fatal("replacement disconnected live call")
	case <-time.After(voiceWatchGrace + 100*time.Millisecond):
	}
	p.mu.Lock()
	count := len(p.watches)
	p.mu.Unlock()
	if count != 1 {
		t.Fatal(count)
	}
	second.Body.Close()
	select {
	case got := <-removed:
		if got != (voiceWatchKey{"lounge", id}) {
			t.Fatal("wrong connection")
		}
	case <-time.After(voiceWatchGrace + time.Second):
		t.Fatal("closed watch not evicted")
	}
	p.mu.Lock()
	count = len(p.watches)
	p.mu.Unlock()
	if count != 0 {
		t.Fatal("watch retained")
	}
	// Reopening this exact connection after its grace period is a new live
	// watch, not another opportunity to retry the previous departure.
	third := open(id)
	p.mu.Lock()
	_, stale := p.hints[id]
	p.mu.Unlock()
	if stale {
		t.Fatal("new watch retained the old departure")
	}
	third.Body.Close()
	select {
	case got := <-removed:
		if got != (voiceWatchKey{"lounge", id}) {
			t.Fatal("wrong reopened connection")
		}
	case <-time.After(voiceWatchGrace + time.Second):
		t.Fatal("reopened watch not cleaned up")
	}
}
