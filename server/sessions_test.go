package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func rememberedRequest(h http.Handler, method string, cookie *http.Cookie) *httptest.ResponseRecorder {
	r := httptest.NewRequest(method, "/api/session", nil)
	r.AddCookie(cookie)
	r.Header.Set("Origin", testConfig().Origin)
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	return w
}
func TestRememberBrowserPersistsAndRevokes(t *testing.T) {
	c := testConfig()
	path := filepath.Join(t.TempDir(), "workspace.json")
	s, _ := openWorkspace(path, "")
	h := newWorkspaceHandler(c, s, nil)
	login := apiCall(h, "POST", "/api/auth", "", map[string]string{"name": "Remember", "password": c.Password})
	mustStatus(t, login, 200)
	cookie := login.Result().Cookies()[0]
	if !cookie.HttpOnly || !cookie.Secure || cookie.SameSite != http.SameSiteStrictMode || cookie.MaxAge != 2592000 {
		t.Fatal("unsafe or nonpersistent cookie")
	}
	var first struct {
		Member     member
		ProfileKey string
	}
	json.Unmarshal(login.Body.Bytes(), &first)
	reopened, err := openWorkspace(path, "")
	if err != nil {
		t.Fatal(err)
	}
	h = newWorkspaceHandler(c, reopened, nil)
	restored := rememberedRequest(h, "GET", cookie)
	mustStatus(t, restored, 200)
	var result struct {
		Member  member
		Session string
	}
	json.Unmarshal(restored.Body.Bytes(), &result)
	if result.Member.ID != first.Member.ID || result.Member.KeyHash != "" || strings.Contains(restored.Body.String(), first.ProfileKey) {
		t.Fatal("identity changed or secret leaked")
	}
	mustStatus(t, apiCall(h, "POST", "/api/join", result.Session, map[string]string{"name": "Remember", "room": "test"}), 200)
	mustStatus(t, apiCall(h, "POST", "/api/join", "invalid", map[string]string{"name": "Remember", "room": "test"}), 401)
	req := httptest.NewRequest("DELETE", "/api/session", nil)
	req.AddCookie(cookie)
	req.Header.Set("Origin", "https://evil.example")
	w := httptest.NewRecorder()
	h.ServeHTTP(w, req)
	mustStatus(t, w, 403)
	mustStatus(t, rememberedRequest(h, "GET", cookie), 200)
	mustStatus(t, rememberedRequest(h, "DELETE", cookie), 200)
	mustStatus(t, rememberedRequest(h, "GET", cookie), 401)
}
func TestRememberRejectsExpiredBlockedAndForged(t *testing.T) {
	s, _ := openWorkspace("", "")
	m, _, _ := s.login("test", "")
	key := randomKey()
	s.data.Sessions = map[string]rememberedSession{keyHash(key): {m.ID, time.Now().Add(time.Hour).Unix()}}
	h := newWorkspaceHandler(testConfig(), s, nil)
	cookie := &http.Cookie{Name: rememberCookie, Value: key}
	s.data.Members[0].Blocked = true
	mustStatus(t, rememberedRequest(h, "GET", cookie), 401)
	s.data.Members[0].Blocked = false
	s.data.Sessions[keyHash(key)] = rememberedSession{m.ID, time.Now().Add(-time.Second).Unix()}
	mustStatus(t, rememberedRequest(h, "GET", cookie), 401)
	cookie.Value = randomKey()
	mustStatus(t, rememberedRequest(h, "GET", cookie), 401)
}

func TestRestoreSelectedProfileKeepsMatchingDeviceSession(t *testing.T) {
	s, _ := openWorkspace("", "")
	memberA, keyA, _ := s.login("A", "")
	_, keyB, _ := s.login("B", "")
	c := testConfig()
	h := newWorkspaceHandler(c, s, nil)
	cookie := sessionCookie(c, randomKey(), int(rememberDays.Seconds()))
	hash := keyHash(cookie.Value)
	entry := rememberedSession{memberA.ID, time.Now().Add(time.Hour).Unix()}
	s.data.Sessions = map[string]rememberedSession{hash: entry}
	request := func(key string, origin string, code int) {
		r := httptest.NewRequest("POST", "/api/session/profile", strings.NewReader(`{"profileKey":"`+key+`"}`))
		r.AddCookie(cookie)
		r.Header.Set("Content-Type", "application/json")
		r.Header.Set("Origin", origin)
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		mustStatus(t, w, code)
		if len(w.Result().Cookies()) != 0 || strings.Contains(w.Body.String(), key) {
			t.Fatal("profile verification changed cookie or exposed profile key")
		}
		if code == 200 {
			var got struct {
				Session string
				Member  member
			}
			if err := json.Unmarshal(w.Body.Bytes(), &got); err != nil || got.Member.ID != memberA.ID || got.Session == "" {
				t.Fatal("verified session does not belong to selected member")
			}
		}
	}
	for i := 0; i < 12; i++ {
		request(keyA, c.Origin, 200)
	}
	request(keyB, c.Origin, 401)
	request(keyA, "https://foreign.example", 403)
	if len(s.data.Sessions) != 1 || s.data.Sessions[hash] != entry {
		t.Fatal("refreshes changed the remembered session")
	}
	s.data.Members[0].Blocked = true
	request(keyA, c.Origin, 401)
}
func TestSeparateAdminCodeSelectsOnlyProvisionedIdentity(t *testing.T) {
	c := testConfig()
	password, key := randomKey(), randomKey()
	t.Setenv("NIMBO_ADMIN_PASSWORD", password)
	t.Setenv("NIMBO_ADMIN_PROFILE_KEY", key)
	s, _ := openWorkspace("", "")
	admin, _, _ := s.login("Администратор · тест", key)
	s.data.Members[0].Role = "admin"
	user, userKey, _ := s.login("User", "")
	h := newWorkspaceHandler(c, s, nil)
	login := func(pass string) *httptest.ResponseRecorder {
		return apiCall(h, "POST", "/api/auth", "", map[string]string{"name": "User", "password": pass, "profileKey": userKey})
	}
	w := login(password)
	mustStatus(t, w, 200)
	var got struct {
		Member     member
		ProfileKey string
	}
	json.Unmarshal(w.Body.Bytes(), &got)
	if got.Member.ID != admin.ID || got.Member.Role != "admin" || got.ProfileKey != key {
		t.Fatal("admin login failed")
	}
	w = login(c.Password)
	mustStatus(t, w, 200)
	json.Unmarshal(w.Body.Bytes(), &got)
	if got.Member.ID != user.ID || got.Member.Role != "member" {
		t.Fatal("ordinary user elevated")
	}
	s.data.Members[0].Role = "member"
	w = login(password)
	mustStatus(t, w, 200)
	json.Unmarshal(w.Body.Bytes(), &got)
	if got.Member.Role != "member" {
		t.Fatal("login restored revoked role")
	}
	s.data.Members[0].Blocked = true
	mustStatus(t, login(password), 403)
}
