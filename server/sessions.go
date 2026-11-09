package main

import (
	"crypto/sha256"
	"fmt"
	"maps"
	"net/http"
	"os"
	"strings"
	"time"
)

const rememberCookie = "nimbo_session"
const rememberDays = 30 * 24 * time.Hour

// Browser cookies are shared between ports on the same host. Give independent
// self-host installations distinct names without invalidating legacy sessions.
func rememberCookieName() string {
	if namespace := os.Getenv("NIMBO_SESSION_NAMESPACE"); namespace != "" {
		sum := sha256.Sum256([]byte(namespace))
		return fmt.Sprintf("%s_%x", rememberCookie, sum[:8])
	}
	return rememberCookie
}

type rememberedSession struct {
	MemberID string `json:"memberId"`
	Expires  int64  `json:"expires"`
}

func sessionCookie(c config, value string, maxAge int) *http.Cookie {
	return &http.Cookie{Name: rememberCookieName(), Value: value, Path: "/api/session", MaxAge: maxAge,
		HttpOnly: true, Secure: strings.HasPrefix(c.Origin, "https://"), SameSite: http.SameSiteStrictMode}
}
func (s *workspaceStore) remember(w http.ResponseWriter, r *http.Request, c config, id string) (string, error) {
	key := randomKey()
	hash := keyHash(key)
	lifetime := rememberDays
	remote := portableClient(r)
	if remote {
		hash = "portable:" + hash
		lifetime = 24 * time.Hour
	}
	err := s.update(func(d *workspaceData) error {
		if d.Sessions == nil {
			d.Sessions = map[string]rememberedSession{}
		}
		if cookie, e := r.Cookie(rememberCookieName()); e == nil {
			delete(d.Sessions, keyHash(cookie.Value))
		}
		now := time.Now().Unix()
		for hash, entry := range d.Sessions {
			if entry.Expires <= now {
				delete(d.Sessions, hash)
			}
		}
		// Bound remembered browsers per identity; retain the most recent logins.
		for {
			count := 0
			oldest := ""
			var expiry int64
			for hash, entry := range d.Sessions {
				if entry.MemberID == id && strings.HasPrefix(hash, "portable:") == remote {
					count++
					if oldest == "" || entry.Expires < expiry {
						oldest = hash
						expiry = entry.Expires
					}
				}
			}
			if count < 8 {
				break
			}
			delete(d.Sessions, oldest)
		}
		d.Sessions[hash] = rememberedSession{id, time.Now().Add(lifetime).Unix()}
		return nil
	})
	if err == nil && !remote {
		http.SetCookie(w, sessionCookie(c, key, int(rememberDays.Seconds())))
	}
	return appToken(c, hash, time.Now().Add(24*time.Hour).Unix()), err
}
func (s *workspaceStore) sessionMember(c config, r *http.Request) string {
	hash := signedSessionKey(c, r)
	s.mu.Lock()
	defer s.mu.Unlock()
	entry := s.data.Sessions[hash]
	if entry.Expires <= time.Now().Unix() {
		return ""
	}
	return entry.MemberID
}
func (s *workspaceStore) revokeSessions(keys ...string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, key := range keys {
		if _, ok := s.data.Sessions[key]; ok {
			next := s.data
			next.Sessions = maps.Clone(s.data.Sessions)
			for _, k := range keys {
				delete(next.Sessions, k)
			}
			if err := s.persist(next); err != nil {
				return err
			}
			s.data.Sessions = next.Sessions
			return nil
		}
	}
	return nil
}
func (s *workspaceStore) registerSessions(mux *http.ServeMux, c config) {
	handler := func(w http.ResponseWriter, r *http.Request) {
		if origin := r.Header.Get("Origin"); origin != "" && origin != c.Origin {
			reject(w, 403, "Недопустимый источник запроса")
			return
		}
		if r.Method != "GET" && r.Method != "DELETE" {
			w.Header().Set("Allow", "GET, DELETE")
			reject(w, 405, "Метод недоступен")
			return
		}
		cookie, err := r.Cookie(rememberCookieName())
		hash := ""
		if err == nil && validKey(cookie.Value) {
			hash = keyHash(cookie.Value)
		}
		if r.Method == "DELETE" {
			if err := s.revokeSessions(hash, signedSessionKey(c, r)); err != nil {
				report(w, err)
				return
			}
			http.SetCookie(w, sessionCookie(c, "", -1))
			jsonReply(w, 200, map[string]bool{"ok": true})
			return
		}
		s.mu.Lock()
		entry, exists := s.data.Sessions[hash]
		m := findMember(&s.data, entry.MemberID)
		var current member
		valid := exists && entry.Expires > time.Now().Unix() && m != nil && !isBlocked(m)
		if valid {
			current = *m
			current.KeyHash = ""
		}
		s.mu.Unlock()
		if !valid {
			http.SetCookie(w, sessionCookie(c, "", -1))
			reject(w, 401, "Войдите в Nimbo")
			return
		}
		jsonReply(w, 200, map[string]any{"member": current, "session": appToken(c, hash, time.Now().Add(24*time.Hour).Unix())})
	}
	mux.HandleFunc("DELETE /api/access-session", func(w http.ResponseWriter, r *http.Request) {
		if !allowedAPIOrigin(c, r) {
			reject(w, 403, "Недопустимый источник запроса")
			return
		}
		if err := s.revokeSessions(signedSessionKey(c, r)); err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, map[string]bool{"ok": true})
	})
	mux.HandleFunc("GET /api/session", handler)
	mux.HandleFunc("DELETE /api/session", handler)
}
