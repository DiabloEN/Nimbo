package main

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"io/fs"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func testConfig() config {
	return config{"127.0.0.1:8090", "https://chat.example.org", "wss://rtc.example.org", "test-key", strings.Repeat("s", 40), "long-test-password"}
}

func join(h http.Handler, body, origin string, session ...string) *httptest.ResponseRecorder {
	r := httptest.NewRequest("POST", "/api/join", strings.NewReader(body))
	r.Header.Set("Content-Type", "application/json")
	r.Header.Set("Origin", origin)
	if len(session) > 0 {
		r.Header.Set("Authorization", "Bearer "+session[0])
	}
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	return w
}

func TestJoinAuthorizationAndGrants(t *testing.T) {
	c := testConfig()
	h := newHandler(c)
	_, session, _ := appLogin(t, h, "Ann", "")
	// Knowing an invitation/password alone must not grant a media token.
	mustStatus(t, join(h, `{"name":"Ann","password":"long-test-password","room":"lounge"}`, c.Origin), 401)
	for _, test := range []struct {
		body, origin string
		status       int
	}{
		{`{"name":"Ann","password":"long-test-password","room":"admin"}`, c.Origin, 403},
		{`{"name":"Ann","password":"long-test-password","room":"lounge"}`, "https://other.example", 403},
		{`{"name":"Ann","password":"long-test-password","room":"lounge","admin":true}`, c.Origin, 400},
		{`{"name":"Ann","password":"long-test-password","room":"lounge"}{}`, c.Origin, 400},
		{`{"name":"\n","password":"long-test-password","room":"lounge"}`, c.Origin, 400},
	} {
		if got := join(h, test.body, test.origin, session).Code; got != test.status {
			t.Fatalf("got %d want %d", got, test.status)
		}
	}
	identities := map[string]bool{}
	for i := 0; i < 2; i++ {
		w := join(h, `{"name":"Саша","room":"lounge"}`, c.Origin, session)
		if w.Code != 200 {
			t.Fatal(w.Code, w.Body.String())
		}
		var response struct{ Token, Identity, URL string }
		_ = json.Unmarshal(w.Body.Bytes(), &response)
		if identities[response.Identity] || response.Identity == "" {
			t.Fatal("identity reused")
		}
		identities[response.Identity] = true
		parts := strings.Split(response.Token, ".")
		if len(parts) != 3 {
			t.Fatal("invalid JWT")
		}
		mac := hmac.New(sha256.New, []byte(c.APISecret))
		mac.Write([]byte(parts[0] + "." + parts[1]))
		signature, _ := base64.RawURLEncoding.DecodeString(parts[2])
		if !hmac.Equal(signature, mac.Sum(nil)) {
			t.Fatal("bad JWT signature")
		}
		payload, _ := base64.RawURLEncoding.DecodeString(parts[1])
		var claims map[string]any
		_ = json.Unmarshal(payload, &claims)
		grant := claims["video"].(map[string]any)
		if grant["room"] != "lounge" || grant["roomJoin"] != true || grant["canPublishData"] != false || grant["roomAdmin"] != nil {
			t.Fatal("incorrect scope", grant)
		}
		if claims["iss"] != c.APIKey || claims["sub"] != response.Identity || claims["exp"].(float64)-claims["iat"].(float64) != 600 {
			t.Fatal("incorrect lifetime/identity")
		}
		if w.Header().Get("Cache-Control") != "no-store" || strings.Contains(w.Body.String(), c.APISecret) || strings.Contains(w.Body.String(), c.Password) {
			t.Fatal("secret leaked or response cached")
		}
	}
}

func TestRateLimitAndBounds(t *testing.T) {
	h := newHandler(testConfig())
	for i := 0; i < 16; i++ {
		w := join(h, `{}`, "")
		if i == 15 && w.Code != 429 {
			t.Fatal("missing throttle")
		}
	}
	h = newHandler(testConfig())
	if got := join(h, strings.Repeat(" ", 5000)+`{}`, "").Code; got != 400 {
		t.Fatal("oversize request accepted")
	}
	l := limiter{clients: map[string]attempt{}}
	now := time.Now()
	for i := 0; i < 15; i++ {
		l.allow("client", now)
	}
	if !l.allow("client", now.Add(time.Minute)) {
		t.Fatal("throttle did not expire")
	}
}

func TestStaticRoutesDoNotExposeDirectoriesOrFallbackAPI(t *testing.T) {
	h := newHandler(testConfig())
	for _, path := range []string{"/api/missing", "/ui/.keep", "/.keep", "/assets/", "/assets/missing.js", "/source.ts"} {
		w := httptest.NewRecorder()
		h.ServeHTTP(w, httptest.NewRequest("GET", path, nil))
		if w.Code != http.StatusNotFound {
			t.Fatalf("%s: expected 404, got %d", path, w.Code)
		}
	}
}

func TestBuiltWebAssets(t *testing.T) {
	if _, err := assets.ReadFile("ui/index.html"); err != nil {
		t.Skip("web assets not built; build-server runs this again after Vite")
	}
	h := newHandler(testConfig())
	w := httptest.NewRecorder()
	h.ServeHTTP(w, httptest.NewRequest("GET", "/", nil))
	if w.Code != 200 || !strings.Contains(w.Body.String(), "type=\"module\"") || w.Header().Get("Cache-Control") != "no-cache" {
		t.Fatal("Vue entry point missing or cached across releases")
	}
	count := 0
	err := fs.WalkDir(assets, "ui/assets", func(path string, entry fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if entry.IsDir() {
			return nil
		}
		count++
		w := httptest.NewRecorder()
		h.ServeHTTP(w, httptest.NewRequest("GET", "/"+strings.TrimPrefix(path, "ui/"), nil))
		if w.Code != 200 || w.Header().Get("Cache-Control") != "public, max-age=31536000, immutable" {
			t.Errorf("built asset unavailable or uncached: %s", path)
		}
		return nil
	})
	if err != nil || count == 0 {
		t.Fatalf("missing built assets: %v", err)
	}
}

func TestConfigRequiresSecurePublicEndpoints(t *testing.T) {
	t.Setenv("NIMBO_TEST_PASSWORD", "long-test-password")
	t.Setenv("LIVEKIT_API_KEY", "test-key")
	t.Setenv("LIVEKIT_API_SECRET", strings.Repeat("s", 40))
	t.Setenv("NIMBO_ORIGIN", "http://chat.example.org")
	t.Setenv("LIVEKIT_URL", "wss://rtc.example.org")
	if _, err := loadConfig(); err == nil {
		t.Fatal("public HTTP accepted")
	}
	t.Setenv("NIMBO_ORIGIN", "http://127.0.0.1:8090")
	t.Setenv("LIVEKIT_URL", "ws://127.0.0.1:7880")
	if _, err := loadConfig(); err != nil {
		t.Fatal(err)
	}
}

func TestPreviewAccessWithoutMediaToken(t *testing.T) {
	c := testConfig()
	h := newHandler(c)
	for _, tc := range []struct {
		body, origin string
		code         int
	}{
		{`{"name":"Tester","password":"wrong"}`, c.Origin, 401},
		{`{"name":"Tester","password":"long-test-password"}`, "https://other.example", 403},
		{`{"name":" ","password":"long-test-password"}`, c.Origin, 400},
		{`{"name":" Tester ","password":"long-test-password"}`, c.Origin, 200},
	} {
		r := httptest.NewRequest("POST", "/api/auth", strings.NewReader(tc.body))
		r.Header.Set("Content-Type", "application/json")
		r.Header.Set("Origin", tc.origin)
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		if w.Code != tc.code {
			t.Fatalf("auth got %d want %d", w.Code, tc.code)
		}
		if w.Code == 200 {
			var result map[string]any
			_ = json.Unmarshal(w.Body.Bytes(), &result)
			if result["name"] != "Tester" || result["session"] == nil || result["token"] != nil || result["url"] != nil {
				t.Fatal("auth must return app access without a media token")
			}
		}
		if w.Header().Get("Cache-Control") != "no-store" {
			t.Fatal("auth response cached")
		}
	}
	for i := 0; i < 13; i++ {
		r := httptest.NewRequest("POST", "/api/auth", strings.NewReader(`{}`))
		r.Header.Set("Content-Type", "application/json")
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		if i == 12 && w.Code != 429 {
			t.Fatal("auth requests not throttled")
		}
	}
}
