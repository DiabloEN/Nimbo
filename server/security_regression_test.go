package main

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestLogoutWritesOnlyOnceAndRevokesOneDevice(t *testing.T) {
	c := testConfig()
	path := filepath.Join(t.TempDir(), "state.json")
	store, err := openWorkspace(path, "")
	if err != nil {
		t.Fatal(err)
	}
	h := newWorkspaceHandler(c, store, nil)
	login := apiCall(h, "POST", "/api/auth", "", map[string]string{"name": "A", "password": c.Password})
	mustStatus(t, login, 200)
	var first struct{ ProfileKey, Session string }
	if err := json.Unmarshal(login.Body.Bytes(), &first); err != nil {
		t.Fatal(err)
	}
	_, other, _ := appLogin(t, h, "A", first.ProfileKey)
	cookie := login.Result().Cookies()[0]
	stamp := time.Unix(100, 0)
	mark := func() {
		if err := os.Chtimes(path, stamp, stamp); err != nil {
			t.Fatal(err)
		}
	}
	unchanged := func() {
		info, err := os.Stat(path)
		if err != nil || !info.ModTime().Equal(stamp) {
			t.Fatal("unexpected database write")
		}
	}
	mark()
	fake := *cookie
	fake.Value = randomKey()
	mustStatus(t, rememberedRequest(h, "DELETE", &fake), 200)
	unchanged()
	done := make(chan int, 2)
	for range 2 {
		go func() { done <- rememberedRequest(h, "DELETE", cookie).Code }()
	}
	for range 2 {
		if <-done != 200 {
			t.Fatal("concurrent logout failed")
		}
	}
	mark()
	mustStatus(t, rememberedRequest(h, "DELETE", cookie), 200)
	unchanged()
	mustStatus(t, rememberedRequest(h, "GET", cookie), 401)
	assertRevoked(t, h, first.Session)
	mustStatus(t, apiCall(h, "GET", "/api/workspace", other, nil), 200)
}
func assertRevoked(t *testing.T, h http.Handler, token string) {
	t.Helper()
	mustStatus(t, apiCall(h, "GET", "/api/messages?channel=general", token, nil), 401)
	mustStatus(t, apiCall(h, "POST", "/api/messages", token, map[string]string{"channel": "general", "text": "test", "clientId": randomKey()}), 401)
	mustStatus(t, apiCall(h, "POST", "/api/join", token, map[string]string{"room": "lounge", "name": "A"}), 401)
}
func TestGuestConfigAndBoundedHistory(t *testing.T) {
	s, _ := openWorkspace("", "")
	h := newWorkspaceHandler(testConfig(), s, nil)
	_, token, _ := appLogin(t, h, "A", "")
	var public map[string]any
	w := apiCall(h, "GET", "/api/config", "", nil)
	mustStatus(t, w, 200)
	if err := json.Unmarshal(w.Body.Bytes(), &public); err != nil || len(public) != 1 || public["serverName"] != s.data.Name {
		t.Fatal("unexpected public fields")
	}
	if err := s.update(func(d *workspaceData) error {
		for i := range 350 {
			d.Messages["general"] = append(d.Messages["general"], chatMessage{ID: fmt.Sprint(i), Text: "original", ReplyTo: "0"})
		}
		return nil
	}); err != nil {
		t.Fatal(err)
	}
	cursor := "0"
	count := 0
	for cursor != "" {
		var page struct {
			Messages []chatMessage
			NextFrom string
		}
		w := apiCall(h, "GET", "/api/messages?paged=1&channel=general&from="+cursor, token, nil)
		mustStatus(t, w, 200)
		if err := json.Unmarshal(w.Body.Bytes(), &page); err != nil || len(page.Messages) != min(100, 350-count) {
			t.Fatal("unexpected page")
		}
		for _, m := range page.Messages {
			if m.ID != fmt.Sprint(count) || m.Reply == nil || m.Reply.Text != "original" {
				t.Fatal("incorrect page boundary or quote")
			}
			count++
		}
		cursor = page.NextFrom
	}
	if count != 350 {
		t.Fatalf("read %d messages", count)
	}
	// Suspend a real HTTP response after its headers, while a second client writes.
	slow := &blockedResponse{ResponseRecorder: httptest.NewRecorder(), entered: make(chan struct{}), resume: make(chan struct{})}
	req := httptest.NewRequest("GET", "/api/messages?channel=general", nil)
	req.Header.Set("Authorization", "Bearer "+token)
	finished := make(chan struct{})
	go func() { h.ServeHTTP(slow, req); close(finished) }()
	<-slow.entered
	defer func() { close(slow.resume); <-finished }()
	posted := make(chan int, 1)
	go func() {
		posted <- apiCall(h, "POST", "/api/messages", token, map[string]string{"channel": "general", "text": "concurrent", "clientId": randomKey()}).Code
	}()
	select {
	case code := <-posted:
		if code != 200 {
			t.Fatal(code)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("history transmission blocked a writer")
	}
}

type blockedResponse struct {
	*httptest.ResponseRecorder
	entered, resume chan struct{}
}

func (w *blockedResponse) Write(b []byte) (int, error) {
	close(w.entered)
	<-w.resume
	return w.ResponseRecorder.Write(b)
}
func TestPortableBearerLogout(t *testing.T) {
	c := testConfig()
	h := newHandler(c)
	_, local, key := appLogin(t, h, "A", "")
	remote := func(method, path, token, body string) *httptest.ResponseRecorder {
		r := httptest.NewRequest(method, path, strings.NewReader(body))
		r.Header.Set("Origin", "https://client.example")
		r.Header.Set("X-Nimbo-Client", "1")
		r.Header.Set("Authorization", "Bearer "+token)
		r.Header.Set("Content-Type", "application/json")
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		return w
	}
	w := remote("POST", "/api/profile-session", "", "{\"profileKey\":\""+key+"\"}")
	mustStatus(t, w, 200)
	var result struct{ Session string }
	if err := json.Unmarshal(w.Body.Bytes(), &result); err != nil {
		t.Fatal(err)
	}
	mustStatus(t, remote("GET", "/api/workspace", result.Session, ""), 200)
	mustStatus(t, remote("DELETE", "/api/access-session", result.Session, ""), 200)
	assertRevoked(t, h, result.Session)
	mustStatus(t, apiCall(h, "GET", "/api/workspace", local, nil), 200)
}
