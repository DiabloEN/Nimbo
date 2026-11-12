package main

import (
	"encoding/json"
	"path/filepath"
	"strings"
	"testing"
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
