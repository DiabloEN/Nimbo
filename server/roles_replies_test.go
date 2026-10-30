package main

import (
	"encoding/json"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestRolesPermissionsAssignmentsRevocation(t *testing.T) {
	s, _ := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	h := newWorkspaceHandler(testConfig(), s, nil)
	owner, ot, _ := appLogin(t, h, "Owner", "")
	admin, at, _ := appLogin(t, h, "Admin", "")
	person, pt, _ := appLogin(t, h, "Person", "")
	other, _, _ := appLogin(t, h, "Other", "")
	s.update(func(d *workspaceData) error {
		findMember(d, owner.ID).Role = "owner"
		findMember(d, admin.ID).Role = "admin"
		return nil
	})
	create := func(token, name string, permissions []string) workspaceRole {
		r := apiCall(h, "POST", "/api/roles", token, map[string]any{"name": name, "permissions": permissions})
		mustStatus(t, r, 200)
		var role workspaceRole
		json.Unmarshal(r.Body.Bytes(), &role)
		return role
	}
	channels := create(at, "Channels", []string{"manage_channels"})
	chat := create(at, "Chat", []string{"manage_chat"})
	roles := create(ot, "Role manager", []string{"manage_roles"})
	assign := func(actor, target string, ids []string, code int) {
		mustStatus(t, apiCall(h, "PUT", "/api/members/"+target+"/roles", actor, map[string]any{"roleIds": ids}), code)
	}
	assign(at, person.ID, []string{channels.ID, chat.ID}, 200)
	mustStatus(t, apiCall(h, "POST", "/api/channels", pt, map[string]string{"name": "Allowed", "kind": "voice", "icon": "coffee"}), 200)
	mustStatus(t, apiCall(h, "PATCH", "/api/workspace", pt, map[string]string{"name": "Forbidden"}), 403)
	mustStatus(t, apiCall(h, "POST", "/api/roles", pt, map[string]any{"name": "Escalation", "permissions": allPermissions}), 403)
	assign(at, admin.ID, []string{}, 403)
	assign(at, owner.ID, []string{}, 403)
	assign(ot, person.ID, []string{roles.ID}, 200)
	assign(pt, other.ID, []string{"admin"}, 403)
	mustStatus(t, apiCall(h, "PATCH", "/api/roles/"+roles.ID, pt, map[string]any{"name": "Self", "permissions": allPermissions}), 403)
	assign(ot, person.ID, []string{channels.ID, chat.ID}, 200)
	mustStatus(t, apiCall(h, "DELETE", "/api/roles/"+channels.ID, at, nil), 200)
	mustStatus(t, apiCall(h, "POST", "/api/channels", pt, map[string]string{"name": "Revoked", "kind": "voice"}), 403)
	saved, err := openWorkspace(s.path, "")
	if err != nil || !hasPermission(&saved.data, person.ID, "manage_chat") || hasPermission(&saved.data, person.ID, "manage_channels") {
		t.Fatal("role persistence or additive revocation failed")
	}
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/lounge", at, map[string]string{"name": "Room", "icon": "javascript:alert(1)"}), 400)
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/lounge", at, map[string]string{"name": "Room", "icon": "game"}), 200)
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/lounge", at, map[string]string{"name": "Renamed"}), 200)
	if findChannel(&s.data, "lounge").Icon != "game" {
		t.Fatal("rename erased icon")
	}
}

func TestChatModerationMuteAndReplyPrivacy(t *testing.T) {
	s, _ := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	h := newWorkspaceHandler(testConfig(), s, nil)
	mod, mt, _ := appLogin(t, h, "Moderator", "")
	guest, gt, _ := appLogin(t, h, "Guest", "")
	owner, ot, _ := appLogin(t, h, "Owner", "")
	s.update(func(d *workspaceData) error {
		findMember(d, mod.ID).RoleIDs = []string{"moderator"}
		findMember(d, owner.ID).Role = "owner"
		return nil
	})
	post := func(token, text, reply string) chatMessage {
		r := apiCall(h, "POST", "/api/messages", token, map[string]string{"channel": "general", "text": text, "clientId": randomKey(), "replyTo": reply})
		mustStatus(t, r, 200)
		var m chatMessage
		json.Unmarshal(r.Body.Bytes(), &m)
		return m
	}
	original := post(gt, "secret original", "")
	response := post(mt, "A reply", original.ID)
	if response.Reply == nil || response.Reply.Text != "secret original" {
		t.Fatal("reply missing")
	}
	mustStatus(t, apiCall(h, "POST", "/api/messages", gt, map[string]string{"channel": "general", "text": "bad reply", "clientId": randomKey(), "replyTo": "missing"}), 400)
	mute := func(token, target, action string, seconds int, code int) {
		mustStatus(t, apiCall(h, "POST", "/api/members/"+target+"/moderate", token, map[string]any{"action": action, "seconds": seconds}), code)
	}
	mute(gt, mod.ID, "mute", 900, 403)
	mute(mt, owner.ID, "mute", 900, 403)
	mute(mt, guest.ID, "mute", 0, 400)
	mute(mt, guest.ID, "mute", 604801, 400)
	mute(mt, guest.ID, "mute", 900, 200)
	mustStatus(t, apiCall(h, "POST", "/api/messages", gt, map[string]string{"channel": "general", "text": "blocked", "clientId": randomKey()}), 403)
	mustStatus(t, apiCall(h, "PUT", "/api/messages/"+original.ID+"/reactions", gt, map[string]any{"channel": "general", "emoji": "👍", "active": true}), 403)
	mute(mt, guest.ID, "unmute", 0, 200)
	post(gt, "unmuted", "")
	mute(mt, guest.ID, "mute", 900, 200)
	s.update(func(d *workspaceData) error {
		findMember(d, guest.ID).ChatMutedUntil = time.Now().Add(-time.Second).UnixMilli()
		return nil
	})
	post(gt, "expired", "")
	// Authors may delete their own messages, but not another participant's.
	mustStatus(t, apiCall(h, "DELETE", "/api/messages/"+response.ID+"?channel=general", gt, nil), 403)
	self := post(gt, "own removable message", "")
	mustStatus(t, apiCall(h, "DELETE", "/api/messages/"+self.ID+"?channel=general", gt, nil), 200)
	mustStatus(t, apiCall(h, "DELETE", "/api/messages/"+original.ID+"?channel=general", mt, nil), 200)
	history := apiCall(h, "GET", "/api/messages?channel=general", ot, nil)
	mustStatus(t, history, 200)
	if strings.Contains(history.Body.String(), "secret original") {
		t.Fatal("deleted text leaked into quoted reply")
	}
	var messages []chatMessage
	json.Unmarshal(history.Body.Bytes(), &messages)
	if len(messages) < 1 || messages[0].ID != response.ID || messages[0].Reply == nil || !messages[0].Reply.Deleted {
		t.Fatal("deletion not reflected")
	}
	persisted, _ := openWorkspace(s.path, "")
	for _, m := range persisted.data.Messages["general"] {
		if m.ID == original.ID || m.ID == self.ID || strings.Contains(m.Text, "secret original") {
			t.Fatal("deleted text persisted")
		}
	}
}
