package main

import (
	"encoding/json"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestChatRetentionPaginationAndReactions(t *testing.T) {
	path := filepath.Join(t.TempDir(), "workspace.json")
	s, _ := openWorkspace(path, "")
	h := newWorkspaceHandler(testConfig(), s, nil)
	a, token, _ := appLogin(t, h, "A", "")
	b, other, _ := appLogin(t, h, "B", "")
	s.update(func(d *workspaceData) error {
		for i := 0; i < 105; i++ {
			d.Messages["general"] = append(d.Messages["general"], chatMessage{ID: randomKey(), AuthorID: a.ID, Text: "history", Name: "A", CreatedAt: int64(i), ClientID: randomKey()})
		}
		return nil
	})
	w := apiCall(h, "POST", "/api/messages", token, map[string]string{"channel": "general", "text": "Newest", "clientId": randomKey()})
	mustStatus(t, w, 200)
	saved, err := openWorkspace(path, "")
	if err != nil || len(saved.data.Messages["general"]) != 106 {
		t.Fatal("retention still deletes history", err)
	}
	var page struct {
		Messages []chatMessage
		HasMore  bool
	}
	w = apiCall(h, "GET", "/api/messages?paged=1&channel=general", token, nil)
	mustStatus(t, w, 200)
	json.Unmarshal(w.Body.Bytes(), &page)
	if len(page.Messages) != 100 || !page.HasMore {
		t.Fatal("incorrect latest page")
	}
	first := page.Messages[0].ID
	w = apiCall(h, "GET", "/api/messages?paged=1&channel=general&before="+first, token, nil)
	json.Unmarshal(w.Body.Bytes(), &page)
	if len(page.Messages) != 6 || page.HasMore {
		t.Fatal("older history inaccessible")
	}
	id := page.Messages[0].ID
	url := "/api/messages/" + id + "/reactions"
	body := map[string]any{"channel": "general", "emoji": "👍", "active": true}
	mustStatus(t, apiCall(h, "PUT", url, "", body), 401)
	for _, who := range []string{token, token, other} {
		mustStatus(t, apiCall(h, "PUT", url, who, body), 200)
	}
	if len(s.data.Messages["general"][0].Reactions["👍"]) != 2 {
		t.Fatal("reaction retry duplicated or lost a user")
	}
	body["active"] = false
	mustStatus(t, apiCall(h, "PUT", url, token, body), 200)
	users := s.data.Messages["general"][0].Reactions["👍"]
	if len(users) != 1 || users[0] != b.ID {
		t.Fatal("removing reaction affected another user")
	}
	body["emoji"] = "<script>"
	mustStatus(t, apiCall(h, "PUT", url, token, body), 400)
	saved, err = openWorkspace(path, "")
	if err != nil || len(saved.data.Messages["general"][0].Reactions["👍"]) != 1 {
		t.Fatal("reaction was not persisted")
	}
}

func TestAdminMayPromoteButCannotEscalateOrDemote(t *testing.T) {
	s, _ := openWorkspace("", "")
	h := newWorkspaceHandler(testConfig(), s, nil)
	a, at, _ := appLogin(t, h, "Admin", "")
	b, bt, _ := appLogin(t, h, "Member", "")
	s.update(func(d *workspaceData) error { findMember(d, a.ID).Role = "admin"; return nil })
	mustStatus(t, apiCall(h, "PATCH", "/api/members/"+b.ID, bt, map[string]string{"role": "admin"}), 403)
	mustStatus(t, apiCall(h, "PATCH", "/api/members/"+b.ID, at, map[string]string{"role": "admin"}), 200)
	mustStatus(t, apiCall(h, "PATCH", "/api/members/"+a.ID, bt, map[string]string{"role": "member"}), 403)
	mustStatus(t, apiCall(h, "PATCH", "/api/members/"+a.ID, bt, map[string]string{"role": "owner"}), 403)
	mustStatus(t, apiCall(h, "POST", "/api/owner", at, map[string]string{"key": s.ownerKey}), 403)
}

func TestReconnectingHintOwnershipAndExpiry(t *testing.T) {
	s, _ := openWorkspace("", "")
	p := newPresence(testConfig(), s, "")
	p.checked = time.Now()
	h := newWorkspaceHandler(testConfig(), s, p)
	m, token, _ := appLogin(t, h, "A", "")
	id := m.ID + ".tab"
	p.rooms["lounge"] = voicePresence{SID: "room", Since: 1, People: []voicePerson{{Identity: id, Name: m.Name}}}
	body := map[string]string{"room": "lounge", "identity": "other.tab", "state": "reconnecting"}
	mustStatus(t, apiCall(h, "POST", "/api/voice-state", token, body), 400)
	body["identity"] = id
	mustStatus(t, apiCall(h, "POST", "/api/voice-state", token, body), 200)
	rooms, _ := p.snapshot()
	if !rooms["lounge"].People[0].Reconnecting || p.rooms["lounge"].People[0].Reconnecting {
		t.Fatal("hint missing or mutated raw snapshot")
	}
	delete(p.rooms, "lounge")
	rooms, _ = p.snapshot()
	if len(rooms["lounge"].People) != 0 {
		t.Fatal("hint resurrected a participant absent from LiveKit")
	}
	p.rooms["lounge"] = voicePresence{People: []voicePerson{{Identity: id}}}
	hint := p.hints[id]
	hint.Until = time.Now().Add(-time.Second)
	p.hints[id] = hint
	rooms, _ = p.snapshot()
	if len(rooms["lounge"].People) != 1 || rooms["lounge"].People[0].Reconnecting {
		t.Fatal("expired hint still marks an active participant as reconnecting")
	}
	body["state"] = "left"
	mustStatus(t, apiCall(h, "POST", "/api/voice-state", token, body), 200)
	p.rooms["lounge"] = voicePresence{People: []voicePerson{{Identity: id}}}
	rooms, _ = p.snapshot()
	if len(rooms["lounge"].People) != 0 {
		t.Fatal("explicit leave still visible")
	}
	if rooms["lounge"].Since != 0 {
		t.Fatal("empty room retained activity timer")
	}
	// Forged origins do not reach the authenticated hint handler.
	r := httptest.NewRequest("POST", "/api/voice-state", strings.NewReader(`{}`))
	r.Header.Set("Origin", "https://evil.example")
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	mustStatus(t, w, 403)
}
