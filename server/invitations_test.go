package main

import (
	"encoding/base64"
	"encoding/json"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestInvitationLabels(t *testing.T) {
	path := filepath.Join(t.TempDir(), "workspace.json")
	store, err := openWorkspace(path, "")
	if err != nil {
		t.Fatal(err)
	}
	h := newWorkspaceHandler(testConfig(), store, nil)
	owner, session, _ := appLogin(t, h, "Owner", "")
	if err := store.assignOwner(owner.ID, false); err != nil {
		t.Fatal(err)
	}
	create := func(body map[string]any) invitation {
		t.Helper()
		reply := apiCall(h, "POST", "/api/invitations", session, body)
		mustStatus(t, reply, 201)
		var item invitation
		if err := json.Unmarshal(reply.Body.Bytes(), &item); err != nil {
			t.Fatal(err)
		}
		return item
	}
	personal := create(map[string]any{"singleUse": true, "label": "  Для команды  "})
	raw, err := base64.RawURLEncoding.DecodeString(personal.Code)
	if err != nil || len(raw) != 16 || len(personal.Code) != 22 {
		t.Fatal("expected a 128-bit URL-safe invitation code")
	}
	general := create(map[string]any{"seconds": 3600})
	if personal.Label != "Для команды" || !personal.SingleUse || personal.CreatedAt == 0 ||
		general.Label != "" || general.SingleUse || general.ExpiresAt <= general.CreatedAt {
		t.Fatal("incorrect invitation metadata")
	}
	for _, invalid := range []string{strings.Repeat("я", 81), "две\nстроки"} {
		mustStatus(t, apiCall(h, "POST", "/api/invitations", session, map[string]string{"label": invalid}), 400)
	}
	reopened, err := openWorkspace(path, "")
	if err != nil {
		t.Fatal(err)
	}
	for _, item := range activeInvitations(&reopened.data) {
		if item == personal {
			return
		}
	}
	t.Fatal("saved invitation lost its metadata")
}

func TestPersonalInvitationConcurrentMembership(t *testing.T) {
	s, err := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	if err != nil {
		t.Fatal(err)
	}
	code := newInvitationCode()
	if err := s.ensureInvitations(code); err != nil {
		t.Fatal(err)
	}
	if err := s.update(func(d *workspaceData) error {
		d.Invitations[0].SingleUse = true
		return nil
	}); err != nil {
		t.Fatal(err)
	}
	start := make(chan struct{})
	joined := make(chan bool, 2)
	for i := 0; i < 2; i++ {
		go func() {
			<-start
			_, _, err := s.login("Test", randomKey(), code)
			joined <- err == nil
		}()
	}
	close(start)
	a, b := <-joined, <-joined
	if a == b || len(s.data.Invitations) != 0 || len(s.data.Members) != 1 {
		t.Fatal("personal invitation must create exactly one membership")
	}
}

func TestInvitationReturnRevokeExpiry(t *testing.T) {
	s, err := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	if err != nil {
		t.Fatal(err)
	}
	h := newWorkspaceHandler(testConfig(), s, nil)
	owner, session, _ := appLogin(t, h, "Owner", "")
	if err := s.assignOwner(owner.ID, false); err != nil {
		t.Fatal(err)
	}
	member, _, key := appLogin(t, h, "Existing", "")
	w := apiCall(h, "POST", "/api/invitations", session, map[string]any{"singleUse": true})
	mustStatus(t, w, 201)
	var item invitation
	if err := json.Unmarshal(w.Body.Bytes(), &item); err != nil {
		t.Fatal(err)
	}
	credential := map[string]string{"name": "Existing", "password": item.Code, "profileKey": key}
	mustStatus(t, apiCall(h, "POST", "/api/auth", "", credential), 200)
	if invitationForCode(&s.data, item.Code) < 0 {
		t.Fatal("existing member consumed a personal invitation")
	}
	mustStatus(t, apiCall(h, "POST", "/api/members/"+member.ID+"/moderate", session, map[string]string{"action": "block"}), 200)
	mustStatus(t, apiCall(h, "POST", "/api/profile-session", "", map[string]string{"profileKey": key}), 401)
	mustStatus(t, apiCall(h, "POST", "/api/auth", "", credential), 403)
	if invitationForCode(&s.data, item.Code) < 0 {
		t.Fatal("blocked member consumed a personal invitation")
	}
	mustStatus(t, apiCall(h, "DELETE", "/api/invitations/"+item.ID, session, nil), 200)
	other := map[string]string{"name": "New", "password": item.Code}
	mustStatus(t, apiCall(h, "POST", "/api/auth", "", other), 401)
	w = apiCall(h, "POST", "/api/invitations", session, map[string]any{"singleUse": true, "seconds": 60})
	mustStatus(t, w, 201)
	if err := json.Unmarshal(w.Body.Bytes(), &item); err != nil {
		t.Fatal(err)
	}
	if err := s.update(func(d *workspaceData) error {
		d.Invitations[invitationForCode(d, item.Code)].ExpiresAt = time.Now().Add(-time.Second).UnixMilli()
		return nil
	}); err != nil {
		t.Fatal(err)
	}
	other["password"] = item.Code
	mustStatus(t, apiCall(h, "POST", "/api/auth", "", other), 401)
	if len(s.data.Members) != 2 {
		t.Fatal("return or inactive invitation created unexpected membership")
	}
}
