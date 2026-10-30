package main

import (
	"path/filepath"
	"testing"
)

func TestOwnerManagesAdministrators(t *testing.T) {
	s, _ := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	h := newWorkspaceHandler(testConfig(), s, nil)
	owner, ot, _ := appLogin(t, h, "Owner", "")
	admin, at, _ := appLogin(t, h, "Admin", "")
	peer, pt, _ := appLogin(t, h, "Peer", "")
	s.update(func(d *workspaceData) error {
		findMember(d, admin.ID).Role = "admin"
		findMember(d, peer.ID).Role = "admin"
		return nil
	})
	if err := s.assignOwner(owner.ID, false); err != nil {
		t.Fatal(err)
	}
	if err := s.assignOwner(admin.ID, false); err == nil {
		t.Fatal("second owner accepted")
	}
	roles := map[string]any{"roleIds": []string{}}
	mustStatus(t, apiCall(h, "PUT", "/api/members/"+admin.ID+"/roles", pt, roles), 403)
	mustStatus(t, apiCall(h, "PUT", "/api/members/"+admin.ID+"/roles", ot, roles), 200)
	mustStatus(t, apiCall(h, "PATCH", "/api/workspace", at, map[string]string{"name": "Forbidden"}), 403)
	block := map[string]string{"action": "block"}
	mustStatus(t, apiCall(h, "POST", "/api/members/"+owner.ID+"/moderate", pt, block), 403)
	mustStatus(t, apiCall(h, "POST", "/api/members/"+owner.ID+"/moderate", ot, block), 403)
	mustStatus(t, apiCall(h, "POST", "/api/members/"+peer.ID+"/moderate", ot, block), 200)
	if !findMember(&s.data, peer.ID).Blocked {
		t.Fatal("owner did not block admin")
	}
}
