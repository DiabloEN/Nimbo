package main

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func apiCall(h http.Handler, method, path, token string, body any) *httptest.ResponseRecorder {
	b, _ := json.Marshal(body)
	r := httptest.NewRequest(method, path, strings.NewReader(string(b)))
	r.Header.Set("Content-Type", "application/json")
	r.Header.Set("Origin", testConfig().Origin)
	if token != "" {
		r.Header.Set("Authorization", "Bearer "+token)
	}
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	return w
}
func mustStatus(t *testing.T, w *httptest.ResponseRecorder, code int) {
	t.Helper()
	if w.Code != code {
		t.Fatalf("status %d, expected %d: %s", w.Code, code, w.Body.String())
	}
}
func appLogin(t *testing.T, h http.Handler, name, key string) (member, string, string) {
	t.Helper()
	w := apiCall(h, "POST", "/api/auth", "", map[string]string{"name": name, "password": testConfig().Password, "profileKey": key})
	mustStatus(t, w, 200)
	var result struct {
		Member              member
		Session, ProfileKey string
	}
	_ = json.Unmarshal(w.Body.Bytes(), &result)
	if result.Member.KeyHash != "" || !validKey(result.ProfileKey) || result.Session == "" {
		t.Fatal("invalid member credential response")
	}
	return result.Member, result.Session, result.ProfileKey
}
func TestWorkspacePermissionsPersistenceAndChat(t *testing.T) {
	path := filepath.Join(t.TempDir(), "workspace.json")
	ownerKey := randomKey()
	store, err := openWorkspace(path, ownerKey)
	if err != nil {
		t.Fatal(err)
	}
	h := newWorkspaceHandler(testConfig(), store, nil)
	owner, ownerSession, profileKey := appLogin(t, h, "Owner", "")
	admin, adminSession, _ := appLogin(t, h, "Admin", "")
	_, guestSession, _ := appLogin(t, h, "Guest", "")
	if owner.Role != "member" {
		t.Fatal("first visitor became owner")
	}
	mustStatus(t, apiCall(h, "GET", "/api/workspace", "", nil), 401)
	mustStatus(t, apiCall(h, "POST", "/api/channels", guestSession, map[string]string{"name": "bad", "kind": "text"}), 403)
	mustStatus(t, apiCall(h, "POST", "/api/owner", ownerSession, map[string]string{"key": "wrong"}), 403)
	mustStatus(t, apiCall(h, "POST", "/api/owner", ownerSession, map[string]string{"key": ownerKey}), 403)
	if err := store.assignOwner(owner.ID, false); err != nil {
		t.Fatal(err)
	}
	mustStatus(t, apiCall(h, "POST", "/api/owner", guestSession, map[string]string{"key": ownerKey}), 403)
	mustStatus(t, apiCall(h, "PATCH", "/api/members/"+admin.ID, guestSession, map[string]string{"role": "admin"}), 403)
	mustStatus(t, apiCall(h, "PATCH", "/api/members/"+admin.ID, ownerSession, map[string]string{"role": "admin"}), 200)
	mustStatus(t, apiCall(h, "PATCH", "/api/members/"+owner.ID, adminSession, map[string]string{"role": "member"}), 403)
	mustStatus(t, apiCall(h, "PATCH", "/api/members/"+owner.ID, ownerSession, map[string]string{"role": "member"}), 400)
	w := apiCall(h, "POST", "/api/channels", adminSession, map[string]string{"name": "Discussion", "kind": "text"})
	mustStatus(t, w, 200)
	var ch channel
	json.Unmarshal(w.Body.Bytes(), &ch)
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/"+ch.ID, adminSession, map[string]string{"name": "Renamed", "description": "Topic"}), 200)
	message := map[string]string{"channel": ch.ID, "text": "Hello <script>alert(1)</script>", "clientId": randomKey()}
	w = apiCall(h, "POST", "/api/messages", guestSession, message)
	mustStatus(t, w, 200)
	var first chatMessage
	json.Unmarshal(w.Body.Bytes(), &first)
	w = apiCall(h, "POST", "/api/messages", guestSession, message)
	mustStatus(t, w, 200)
	var retry chatMessage
	json.Unmarshal(w.Body.Bytes(), &retry)
	if retry.ID != first.ID {
		t.Fatal("retry duplicated message")
	}
	mustStatus(t, apiCall(h, "POST", "/api/messages", guestSession, map[string]string{"channel": "lounge", "text": "No", "clientId": randomKey()}), 404)
	mustStatus(t, apiCall(h, "PATCH", "/api/members/"+admin.ID, ownerSession, map[string]string{"role": "member"}), 200)
	mustStatus(t, apiCall(h, "POST", "/api/channels", adminSession, map[string]string{"name": "No", "kind": "voice"}), 403) // same session, immediately revoked
	saved, err := openWorkspace(path, ownerKey)
	if err != nil {
		t.Fatal(err)
	}
	h = newWorkspaceHandler(testConfig(), saved, nil)
	restored, restoredSession, restoredKey := appLogin(t, h, "Owner Mac", profileKey)
	if restored.ID != owner.ID || restored.Role != "owner" || restoredKey != profileKey {
		t.Fatal("profile role did not survive restart/device restore")
	}
	w = apiCall(h, "GET", "/api/messages?channel="+ch.ID, guestSession, nil)
	mustStatus(t, w, 200)
	var history []chatMessage
	json.Unmarshal(w.Body.Bytes(), &history)
	if len(history) != 1 || history[0].Text != message["text"] {
		t.Fatal("history was not durable")
	}
	w = apiCall(h, "GET", "/api/workspace", guestSession, nil)
	mustStatus(t, w, 200)
	if strings.Contains(w.Body.String(), "keyHash") || strings.Contains(w.Body.String(), profileKey) {
		t.Fatal("roster leaked credentials")
	}
	mustStatus(t, apiCall(h, "DELETE", "/api/channels/"+ch.ID, restoredSession, nil), 200)
	mustStatus(t, apiCall(h, "GET", "/api/messages?channel="+ch.ID, guestSession, nil), 404)
	if len(saved.data.Messages[ch.ID]) != 0 {
		t.Fatal("deleted channel retained message storage")
	}
}
func TestWorkspaceRejectsForgedExpiredCrossOriginAndFailedWrite(t *testing.T) {
	s, _ := openWorkspace("", randomKey())
	h := newWorkspaceHandler(testConfig(), s, nil)
	m, token, _ := appLogin(t, h, "Tester", "")
	mustStatus(t, apiCall(h, "GET", "/api/workspace", token+"bad", nil), 401)
	mustStatus(t, apiCall(h, "GET", "/api/workspace", appToken(testConfig(), m.ID, time.Now().Add(-time.Second).Unix()), nil), 401)
	r := httptest.NewRequest("GET", "/api/workspace", nil)
	r.Header.Set("Authorization", "Bearer "+token)
	r.Header.Set("Origin", "https://evil.example")
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	mustStatus(t, w, 403)
	s.path = filepath.Join(t.TempDir(), "not-a-directory", "workspace.json")
	os.WriteFile(filepath.Dir(s.path), []byte("file"), 0600)
	err := s.update(func(data *workspaceData) error { data.Name = "Must not commit"; return nil })
	if err == nil || s.data.Name != "Свои люди" {
		t.Fatal("failed disk write changed state")
	}
}
func TestDynamicVoiceRoomsAndArchive(t *testing.T) {
	s, _ := openWorkspace("", randomKey())
	h := newWorkspaceHandler(testConfig(), s, nil)
	m, token, _ := appLogin(t, h, "Owner", "")
	s.update(func(d *workspaceData) error { findMember(d, m.ID).Role = "owner"; return nil })
	w := apiCall(h, "POST", "/api/channels", token, map[string]string{"name": "New voice", "kind": "voice"})
	mustStatus(t, w, 200)
	var ch channel
	json.Unmarshal(w.Body.Bytes(), &ch)
	body := map[string]string{"name": "Owner", "password": testConfig().Password, "room": ch.ID}
	w = apiCall(h, "POST", "/api/join", token, body)
	mustStatus(t, w, 200)
	if !strings.Contains(w.Body.String(), m.ID+".") {
		t.Fatal("media identity not bound to profile")
	}
	mustStatus(t, apiCall(h, "DELETE", "/api/channels/"+ch.ID, token, nil), 200)
	mustStatus(t, apiCall(h, "POST", "/api/join", token, body), 403)
}
func TestPresenceTimerSurvivesFirstParticipantLeavingAndResets(t *testing.T) {
	people := `[{"identity":"one","name":"One","joined_at":"100"}]`
	media := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if strings.HasSuffix(r.URL.Path, "ListRooms") {
			ioReply(w, `{"rooms":[{"sid":"RM_test","name":"lounge"}]}`)
		} else {
			ioReply(w, `{"participants":`+people+`}`)
		}
	}))
	defer media.Close()
	s, _ := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	// Presence accepts only members of this workspace, not arbitrary media identities.
	if err := s.update(func(d *workspaceData) error {
		for _, id := range []string{"one", "two", "three"} {
			d.Members = append(d.Members, member{ID: id, Name: id, Role: "member"})
		}
		return nil
	}); err != nil {
		t.Fatal(err)
	}
	p := newPresence(testConfig(), s, media.URL)
	if err := p.poll(context.Background()); err != nil {
		t.Fatal(err)
	}
	rooms, fresh := p.snapshot()
	if !fresh || rooms["lounge"].Since != 100000 {
		t.Fatal("wrong first join timer")
	}
	people = `[{"identity":"two","name":"Two","joinedAt":200}]`
	if err := p.poll(context.Background()); err != nil {
		t.Fatal(err)
	}
	rooms, _ = p.snapshot()
	if rooms["lounge"].Since != 100000 {
		t.Fatal("timer reset when first participant left")
	}
	s2, _ := openWorkspace(s.path, "")
	p2 := newPresence(testConfig(), s2, media.URL)
	p2.poll(context.Background())
	rooms, _ = p2.snapshot()
	if rooms["lounge"].Since != 100000 {
		t.Fatal("timer reset after API restart")
	}
	people = `[]`
	p2.poll(context.Background())
	rooms, _ = p2.snapshot()
	if len(rooms) != 0 {
		t.Fatal("empty room still active")
	}
	people = `[{"identity":"three","name":"Three","joinedAt":"300"}]`
	p2.poll(context.Background())
	rooms, _ = p2.snapshot()
	if rooms["lounge"].Since != 300000 {
		t.Fatal("new activity interval reused old timer")
	}
}
func ioReply(w http.ResponseWriter, b string) {
	w.Header().Set("Content-Type", "application/json")
	w.Write([]byte(b))
}
