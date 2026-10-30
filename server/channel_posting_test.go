package main

import (
	"encoding/json"
	"path/filepath"
	"testing"
)

func TestChannelPostingEnforcedAndPersistent(t *testing.T) {
	s, err := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	if err != nil {
		t.Fatal(err)
	}
	h := newWorkspaceHandler(testConfig(), s, nil)
	owner, ownerToken, _ := appLogin(t, h, "Owner", "")
	if err = s.assignOwner(owner.ID, false); err != nil {
		t.Fatal(err)
	}
	member, memberToken, _ := appLogin(t, h, "Reader", "")
	admin, adminToken, _ := appLogin(t, h, "Admin", "")
	if err = s.update(func(d *workspaceData) error {
		findMember(d, admin.ID).Role = "admin"
		findMember(d, admin.ID).RoleIDs = []string{"admin"}
		return nil
	}); err != nil {
		t.Fatal(err)
	}
	roleResponse := apiCall(h, "POST", "/api/roles", ownerToken, map[string]any{"name": "Authors", "permissions": []string{}})
	mustStatus(t, roleResponse, 200)
	var role workspaceRole
	if err = json.Unmarshal(roleResponse.Body.Bytes(), &role); err != nil {
		t.Fatal(err)
	}
	policy := map[string]any{"name": "News", "posting": channelPostingPolicy{Mode: "roles", RoleIDs: []string{role.ID}}}
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/general", memberToken, policy), 403)
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/general", ownerToken, policy), 200)
	send := func(token string, intended int) {
		t.Helper()
		mustStatus(t, apiCall(h, "POST", "/api/messages", token, map[string]any{"channel": "general", "text": "news", "clientId": randomKey()}), intended)
	}
	send(memberToken, 403)
	send(adminToken, 403)
	send(ownerToken, 200)
	mustStatus(t, apiCall(h, "GET", "/api/messages?channel=general", memberToken, nil), 200)
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/general/files", ownerToken, filePolicy{Enabled: true, Scan: false}), 200)
	mustStatus(t, batchFileRequest(t, h, memberToken, randomKey(), []string{"blocked"}), 403)
	if err = s.update(func(d *workspaceData) error { findMember(d, member.ID).RoleIDs = []string{role.ID}; return nil }); err != nil {
		t.Fatal(err)
	}
	send(memberToken, 200)
	mustStatus(t, batchFileRequest(t, h, memberToken, randomKey(), []string{"allowed"}), 200)
	restored, err := openWorkspace(s.path, "")
	if err != nil {
		t.Fatal(err)
	}
	if p := findChannel(&restored.data, "general").Posting; p == nil || p.Mode != "roles" || len(p.RoleIDs) != 1 {
		t.Fatal("policy lost on restart")
	}
	// Removing the sole permitted role must keep the channel restricted.
	mustStatus(t, apiCall(h, "DELETE", "/api/roles/"+role.ID, ownerToken, nil), 200)
	send(memberToken, 403)
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/general", ownerToken, map[string]any{"name": "Renamed"}), 200)
	send(memberToken, 403)
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/general", ownerToken, map[string]any{"name": "News", "posting": channelPostingPolicy{Mode: "everyone"}}), 200)
	send(memberToken, 200)
}

func TestChannelPostingRejectsInvalidPolicies(t *testing.T) {
	s, _ := openWorkspace("", "")
	h := newWorkspaceHandler(testConfig(), s, nil)
	m, token, _ := appLogin(t, h, "Owner", "")
	if err := s.assignOwner(m.ID, false); err != nil {
		t.Fatal(err)
	}
	for _, p := range []channelPostingPolicy{{Mode: "unknown"}, {Mode: "roles", RoleIDs: []string{"missing"}}, {Mode: "roles", RoleIDs: []string{"admin", "admin"}}} {
		mustStatus(t, apiCall(h, "PATCH", "/api/channels/general", token, map[string]any{"name": "General", "posting": p}), 400)
	}
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/lounge", token, map[string]any{"name": "Voice", "posting": channelPostingPolicy{Mode: "roles"}}), 400)
	mustStatus(t, apiCall(h, "POST", "/api/channels", token, map[string]any{"name": "News", "kind": "text", "posting": channelPostingPolicy{Mode: "roles", RoleIDs: []string{"admin"}}}), 200)
}
