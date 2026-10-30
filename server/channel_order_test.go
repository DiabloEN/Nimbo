package main

import (
	"encoding/json"
	"path/filepath"
	"testing"
)

func TestChannelOrderAuthorizationValidationAndPersistence(t *testing.T) {
	path := filepath.Join(t.TempDir(), "workspace.json")
	s, _ := openWorkspace(path, "")
	h := newWorkspaceHandler(testConfig(), s, nil)
	m, token, _ := appLogin(t, h, "Manager", "")
	_, guest, _ := appLogin(t, h, "Guest", "")
	s.assignOwner(m.ID, false)
	body := map[string]any{"sections": []string{"text", "voice"}, "channels": []string{"general", "test", "lounge"}}
	mustStatus(t, apiCall(h, "PATCH", "/api/channel-order", guest, body), 403)
	mustStatus(t, apiCall(h, "PATCH", "/api/channel-order", token, body), 200)
	w := apiCall(h, "GET", "/api/workspace", token, nil)
	var result struct {
		SectionOrder []string
		Channels     []channel
	}
	json.Unmarshal(w.Body.Bytes(), &result)
	if result.SectionOrder[0] != "text" || result.Channels[1].ID != "test" {
		t.Fatal("order not exposed")
	}
	saved, err := openWorkspace(path, "")
	if err != nil || saved.data.SectionOrder[0] != "text" || saved.data.Channels[0].ID != "general" {
		t.Fatal("order not persisted")
	}
	body["channels"] = []string{"general", "test", "test"}
	mustStatus(t, apiCall(h, "PATCH", "/api/channel-order", token, body), 409)
	if s.data.Channels[2].ID != "lounge" {
		t.Fatal("invalid ordering changed data")
	}
	body["sections"] = []string{"text", "text"}
	mustStatus(t, apiCall(h, "PATCH", "/api/channel-order", token, body), 400)
}
