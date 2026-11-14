package main

import (
	"encoding/json"
	"path/filepath"
	"testing"
	"time"
)

func TestMediaSyncAcrossAdminDeletion(t *testing.T) {
	s, _ := openWorkspace("", randomKey())
	h := newWorkspaceHandler(testConfig(), s, nil)
	admin, adminSession, _ := appLogin(t, h, "Moderator", "")
	author, session, _ := appLogin(t, h, "Listener", "")
	if err := s.assignOwner(admin.ID, false); err != nil {
		t.Fatal(err)
	}
	id, file, keep := randomKey(), randomKey(), randomKey()
	err := s.update(func(d *workspaceData) error {
		for _, m := range []chatMessage{
			{ID: id, AuthorID: author.ID, Attachment: &fileAttachment{ID: file}},
			{ID: randomKey(), AuthorID: author.ID, Attachment: &fileAttachment{ID: keep}},
		} {
			d.Messages["general"] = append(d.Messages["general"], m)
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	type result struct {
		Cursor  string
		Missing []string
	}
	query := func(body any) result {
		t.Helper()
		w := apiCall(h, "POST", "/api/media-cache/sync", session, body)
		mustStatus(t, w, 200)
		var reply result
		if err := json.Unmarshal(w.Body.Bytes(), &reply); err != nil {
			t.Fatal(err)
		}
		return reply
	}
	before := query(map[string]any{"ids": []string{file, keep}})
	if before.Cursor == "" || len(before.Missing) != 0 {
		t.Fatal(before)
	}
	wake := make(chan string, 1)
	go func() {
		w := apiCall(h, "POST", "/api/media-cache/sync", session, map[string]string{"cursor": before.Cursor})
		var reply result
		_ = json.Unmarshal(w.Body.Bytes(), &reply)
		wake <- reply.Cursor
	}()
	mustStatus(t, apiCall(h, "DELETE", "/api/messages/"+id+"?channel=general", adminSession, nil), 200)
	select {
	case cursor := <-wake:
		if cursor == "" || cursor == before.Cursor {
			t.Fatal("stale revision")
		}
	case <-time.After(time.Second):
		t.Fatal("no prompt invalidation")
	}
	offline := query(map[string]any{"cursor": before.Cursor, "ids": []string{file, keep}})
	if len(offline.Missing) != 1 || offline.Missing[0] != file {
		t.Fatal(offline)
	}
	mustStatus(t, apiCall(h, "POST", "/api/media-cache/sync", "", map[string]any{"ids": []string{keep}}), 401)
}

func TestMediaSyncCursorAfterRestart(t *testing.T) {
	path := filepath.Join(t.TempDir(), "workspace.json")
	open := func() *workspaceStore {
		t.Helper()
		s, err := openWorkspace(path, "")
		if err != nil {
			t.Fatal(err)
		}
		return s
	}
	readCursor := func(s *workspaceStore, previous string) string {
		t.Helper()
		h := newWorkspaceHandler(testConfig(), s, nil)
		_, session, _ := appLogin(t, h, "Reader", "")
		w := apiCall(h, "POST", "/api/media-cache/sync", session, map[string]any{"cursor": previous, "ids": []string{}})
		mustStatus(t, w, 200)
		var reply struct{ Cursor string }
		if err := json.Unmarshal(w.Body.Bytes(), &reply); err != nil || reply.Cursor == "" {
			t.Fatal("missing cursor", err)
		}
		return reply.Cursor
	}
	initial := readCursor(open(), "")
	if readCursor(open(), initial) == initial {
		t.Fatal("reopened workspace reused the old media cursor")
	}
}
