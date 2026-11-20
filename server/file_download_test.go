package main

import (
	"encoding/json"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestFileDownloadGrantScopeReplayAndRevocation(t *testing.T) {
	s, err := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	if err != nil {
		t.Fatal(err)
	}
	c := testConfig()
	h := newWorkspaceHandler(c, s, nil)
	owner, token, _ := appLogin(t, h, "Download owner", "")
	if err := s.assignOwner(owner.ID, false); err != nil {
		t.Fatal(err)
	}
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/general/files", token, filePolicy{Enabled: true}), 200)
	up := fileRequest(t, h, token, "test.txt", "0123456789", randomKey())
	mustStatus(t, up, 200)
	var message chatMessage
	json.Unmarshal(up.Body.Bytes(), &message)
	fileID := message.Attachment.ID
	issue := func() string {
		r := httptest.NewRequest("POST", "/api/files/"+fileID+"/download", nil)
		r.Header.Set("Origin", "https://client.example")
		r.Header.Set("X-Nimbo-Client", "1")
		r.Header.Set("Authorization", "Bearer "+token)
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		mustStatus(t, w, 200)
		var result struct{ Ticket string }
		json.Unmarshal(w.Body.Bytes(), &result)
		if !validKey(result.Ticket) || strings.Contains(w.Body.String(), token) {
			t.Fatal("invalid scoped download grant")
		}
		return result.Ticket
	}
	get := func(method, ticket string) *httptest.ResponseRecorder {
		r := httptest.NewRequest(method, "/api/download/"+ticket, nil)
		r.Header.Set("Range", "bytes=2-5")
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		return w
	}
	mustStatus(t, apiCall(h, "POST", "/api/files/"+fileID+"/download", "", nil), 401)
	mustStatus(t, apiCall(h, "POST", "/api/files/"+randomKey()+"/download", token, nil), 404)
	ticket := issue()
	mustStatus(t, get("HEAD", ticket), 206)
	w := get("GET", ticket)
	mustStatus(t, w, 206)
	if w.Body.String() != "2345" || !strings.HasPrefix(w.Header().Get("Content-Disposition"), "attachment;") || w.Header().Get("Referrer-Policy") != "no-referrer" {
		t.Fatal("not a protected streamed attachment")
	}
	mustStatus(t, get("GET", ticket), 401)
	if len(s.downloads) != 0 {
		t.Fatal("consumed grant retained")
	}
	ticket = issue()
	s.mu.Lock()
	s.downloads[keyHash(ticket)].Until = time.Now().Add(-time.Second)
	s.mu.Unlock()
	mustStatus(t, get("GET", ticket), 401)
	ticket = issue()
	s.mu.Lock()
	findMember(&s.data, owner.ID).Blocked = true
	s.mu.Unlock()
	mustStatus(t, get("GET", ticket), 401)
	s.mu.Lock()
	findMember(&s.data, owner.ID).Blocked = false
	s.mu.Unlock()
	ticket = issue()
	s.mu.Lock()
	saved := s.data.Messages["general"]
	s.data.Messages["general"] = nil
	s.mu.Unlock()
	mustStatus(t, get("GET", ticket), 404)
	s.mu.Lock()
	s.data.Messages["general"] = saved
	s.mu.Unlock()
	ticket = issue()
	s.mu.Lock()
	session := s.downloads[keyHash(ticket)].Session
	s.mu.Unlock()
	if err := s.revokeSessions(session); err != nil {
		t.Fatal(err)
	}
	mustStatus(t, get("GET", ticket), 401)
	if len(s.downloads) != 0 {
		t.Fatal("revoked grant retained")
	}
}

func TestUnusedFileDownloadGrantsExpireAndAreBounded(t *testing.T) {
	s, _ := openWorkspace("", "")
	for i := 0; i < 8; i++ {
		if _, err := s.issueFileDownload(fileDownloadGrant{Member: "member"}, 20*time.Millisecond); err != nil {
			t.Fatal(err)
		}
	}
	if _, err := s.issueFileDownload(fileDownloadGrant{Member: "member"}, 20*time.Millisecond); err == nil {
		t.Fatal("unbounded pending downloads")
	}
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		s.mu.Lock()
		count := len(s.downloads)
		s.mu.Unlock()
		if count == 0 {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatal("expired download grants retained")
}
