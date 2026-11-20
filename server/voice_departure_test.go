package main

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func TestExplicitDepartureRemovesOnlyOneConnection(t *testing.T) {
	s, _ := openWorkspace("", "")
	removed := ""
	room := ""
	api := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/twirp/livekit.RoomService/RemoveParticipant" {
			t.Fatal(r.URL.Path)
		}
		var b struct {
			Identity string
			Room     string
		}
		json.NewDecoder(r.Body).Decode(&b)
		removed = b.Identity
		room = b.Room
		jsonReply(w, 200, map[string]any{})
	}))
	defer api.Close()
	p := newPresence(testConfig(), s, api.URL)
	h := newWorkspaceHandler(testConfig(), s, p)
	m, token, _ := appLogin(t, h, "Shared", "")
	p.rooms["lounge"] = voicePresence{People: []voicePerson{{Identity: m.ID + ".web"}, {Identity: m.ID + ".native"}}}
	body := map[string]string{"room": "lounge", "identity": m.ID + ".native", "state": "left"}
	mustStatus(t, apiCall(h, "POST", "/api/voice-state", token, body), 200)
	if removed != m.ID+".native" || room != "lounge" {
		t.Fatal("wrong connection removed")
	}
	view, _ := p.snapshot()
	if len(view["lounge"].People) != 1 || view["lounge"].People[0].Identity != m.ID+".web" {
		t.Fatal("other session disappeared")
	}
	removed = ""
	body["identity"] = "another.session"
	mustStatus(t, apiCall(h, "POST", "/api/voice-state", token, body), 400)
	if removed != "" {
		t.Fatal("foreign removal")
	}
}
func TestPresenceRetryRemovesDepartedConnection(t *testing.T) {
	s, _ := openWorkspace("", "")
	var id string
	removes := 0
	api := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/twirp/livekit.RoomService/ListRooms":
			jsonReply(w, 200, map[string]any{"rooms": []map[string]string{{"name": "lounge", "sid": "r"}}})
		case "/twirp/livekit.RoomService/ListParticipants":
			jsonReply(w, 200, map[string]any{"participants": []voicePerson{{Identity: id}}})
		case "/twirp/livekit.RoomService/RemoveParticipant":
			removes++
			jsonReply(w, 200, map[string]any{})
		default:
			t.Error(r.URL.Path)
		}
	}))
	defer api.Close()
	p := newPresence(testConfig(), s, api.URL)
	h := newWorkspaceHandler(testConfig(), s, p)
	m, _, _ := appLogin(t, h, "Shared", "")
	id = m.ID + ".native"
	p.hints = map[string]voiceHint{id: {Room: "lounge", Left: true, Until: time.Now().Add(time.Second)}}
	if err := p.poll(context.Background()); err != nil {
		t.Fatal(err)
	}
	if removes != 1 || len(p.rooms) != 0 {
		t.Fatal("departure not retried")
	}
}
func TestPresenceDeduplicatesConnectionNotProfile(t *testing.T) {
	p := newPresence(testConfig(), nil, "")
	p.rooms["a"] = voicePresence{People: []voicePerson{{Identity: "m.one", JoinedAt: 1}, {Identity: "m.two", JoinedAt: 2}}}
	p.rooms["b"] = voicePresence{People: []voicePerson{{Identity: "m.one", JoinedAt: 3}}}
	view, _ := p.snapshot()
	if len(view["a"].People) != 1 || view["a"].People[0].Identity != "m.two" || len(view["b"].People) != 1 {
		t.Fatal("wrong identity reconciliation")
	}
}
