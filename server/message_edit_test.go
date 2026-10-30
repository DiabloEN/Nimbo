package main

import (
	"encoding/json"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestMessageEditOwnershipAndPersistence(t *testing.T) {
	s, _ := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	h := newWorkspaceHandler(testConfig(), s, nil)
	owner, ot, _ := appLogin(t, h, "Owner", "")
	_ = s.assignOwner(owner.ID, false)
	member, mt, _ := appLogin(t, h, "Member", "")
	response := apiCall(h, "POST", "/api/messages", mt, map[string]any{"channel": "general", "text": "original", "clientId": randomKey()})
	mustStatus(t, response, 200)
	var m chatMessage
	_ = json.Unmarshal(response.Body.Bytes(), &m)
	edit := func(token, text, previous, channel string, status int) {
		t.Helper()
		mustStatus(t, apiCall(h, "PATCH", "/api/messages/"+m.ID, token, map[string]any{"channel": channel, "text": text, "previousText": previous}), status)
	}
	edit(ot, "forged", "original", "general", 403)
	edit(mt, "new", "original", "missing", 404)
	edit(mt, "", "original", "general", 400)
	edit(mt, strings.Repeat("я", 8001), "original", "general", 400)
	edit(mt, "new", "original", "general", 200)
	edit(mt, "lost update", "original", "general", 409)
	if s.data.Messages["general"][0].EditedAt == 0 || s.data.Messages["general"][0].CreatedAt != m.CreatedAt {
		t.Fatal("edit timestamps invalid")
	}
	restored, err := openWorkspace(s.path, "")
	if err != nil || restored.data.Messages["general"][0].Text != "new" {
		t.Fatal("edit not persisted")
	}
	_ = s.update(func(d *workspaceData) error {
		findChannel(d, "general").Posting = &channelPostingPolicy{Mode: "roles", RoleIDs: []string{}}
		return nil
	})
	edit(mt, "bypass", "new", "general", 403)
	_ = s.update(func(d *workspaceData) error {
		findChannel(d, "general").Posting = nil
		findMember(d, member.ID).ChatMutedUntil = time.Now().Add(time.Hour).UnixMilli()
		return nil
	})
	edit(mt, "muted", "new", "general", 403)
}
func TestMessageEditAttachmentCaptionPreservesPayload(t *testing.T) {
	s, _ := openWorkspace("", "")
	h := newWorkspaceHandler(testConfig(), s, nil)
	m, token, _ := appLogin(t, h, "Author", "")
	id := randomKey()
	_ = s.update(func(d *workspaceData) error {
		d.Messages["general"] = []chatMessage{{ID: id, AuthorID: m.ID, Text: "caption", Attachments: []fileAttachment{{ID: "file", Name: "notes.txt", Size: 5}}, Reactions: map[string][]string{"👍": {m.ID}}, ReplyTo: "quoted"}}
		return nil
	})
	mustStatus(t, apiCall(h, "PATCH", "/api/messages/"+id, token, map[string]any{"channel": "general", "text": "", "previousText": "caption"}), 200)
	got := s.data.Messages["general"][0]
	if len(got.Attachments) != 1 || len(got.Reactions["👍"]) != 1 || got.ReplyTo != "quoted" || got.Text != "" {
		t.Fatal("edit damaged message payload")
	}
}
