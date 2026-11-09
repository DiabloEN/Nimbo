package main

import (
	"encoding/json"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestPortableClientUsesExplicitKeysNotCookies(t *testing.T) {
	c := testConfig()
	s, _ := openWorkspace("", "")
	h := newWorkspaceHandler(c, s, nil)
	login := apiCall(h, "POST", "/api/auth", "", map[string]string{"name": "Portable", "password": c.Password})
	mustStatus(t, login, 200)
	var access struct{ ProfileKey, Session string }
	json.Unmarshal(login.Body.Bytes(), &access)
	cookie := login.Result().Cookies()[0]
	before := len(s.data.Sessions)
	request := func(method, path, token, body, origin string, marker bool) *httptest.ResponseRecorder {
		r := httptest.NewRequest(method, path, strings.NewReader(body))
		r.Header.Set("Origin", origin)
		r.Header.Set("Content-Type", "application/json")
		r.AddCookie(cookie)
		if marker {
			r.Header.Set("X-Nimbo-Client", "1")
		}
		if token != "" {
			r.Header.Set("Authorization", "Bearer "+token)
		}
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		return w
	}
	origin := "https://client.example:7443"
	w := request("POST", "/api/profile-session", "", `{"profileKey":"`+access.ProfileKey+`"}`, origin, true)
	mustStatus(t, w, 200)
	if len(w.Result().Cookies()) != 0 || len(s.data.Sessions) != before+1 {
		t.Fatal("expected a separate portable session without a cookie")
	}
	if w.Header().Get("Access-Control-Allow-Origin") != origin || w.Header().Get("Access-Control-Allow-Credentials") != "" {
		t.Fatal("unsafe CORS")
	}
	mustStatus(t, request("GET", "/api/workspace", access.Session, "", origin, true), 200)
	mustStatus(t, request("GET", "/api/workspace", "", "", origin, true), 401)
	mustStatus(t, request("GET", "/api/workspace", access.Session, "", origin, false), 403)
	mustStatus(t, request("POST", "/api/profile-session", "", `{"profileKey":"`+randomKey()+`"}`, origin, true), 401)
	for _, path := range []string{"/api/session", "/api/session/media/anything"} {
		mustStatus(t, request("GET", path, access.Session, "", origin, true), 403)
	}
	for _, bad := range []string{"null", "http://public.example", "https://client.example/path", "https://user@client.example"} {
		mustStatus(t, request("GET", "/api/workspace", access.Session, "", bad, true), 403)
	}
	s.data.Members[0].Blocked = true
	mustStatus(t, request("GET", "/api/workspace", access.Session, "", origin, true), 401)
	mustStatus(t, request("POST", "/api/profile-session", "", `{"profileKey":"`+access.ProfileKey+`"}`, origin, true), 401)
}

func TestPortableClientPreflightAndForeignToken(t *testing.T) {
	c := testConfig()
	h := newHandler(c)
	check := func(headers string, code int) {
		r := httptest.NewRequest("OPTIONS", "/api/profile-session", nil)
		r.Header.Set("Origin", "https://client.example")
		r.Header.Set("Access-Control-Request-Method", "POST")
		r.Header.Set("Access-Control-Request-Headers", headers)
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		mustStatus(t, w, code)
	}
	check("content-type,x-nimbo-client", 204)
	check("authorization, x-nimbo-client", 204)
	check("content-type", 403)
	check("x-nimbo-client,cookie", 403)
	_, token, _ := appLogin(t, h, "Member", "")
	other := c
	other.APISecret = strings.Repeat("x", 64)
	otherHandler := newHandler(other)
	r := httptest.NewRequest("GET", "/api/workspace", nil)
	r.Header.Set("Origin", "https://client.example")
	r.Header.Set("X-Nimbo-Client", "1")
	r.Header.Set("Authorization", "Bearer "+token)
	w := httptest.NewRecorder()
	otherHandler.ServeHTTP(w, r)
	mustStatus(t, w, 401)
}
