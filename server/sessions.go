package main

import (
	"crypto/sha256"
	"fmt"
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
func (s *workspaceStore) remember(w http.ResponseWriter, r *http.Request, c config, id string) error {
	key := randomKey()
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
				if entry.MemberID == id {
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
		d.Sessions[keyHash(key)] = rememberedSession{id, time.Now().Add(rememberDays).Unix()}
		return nil
	})
	if err == nil {
		http.SetCookie(w, sessionCookie(c, key, int(rememberDays.Seconds())))
	}
	return err
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
			if hash != "" {
				if err := s.update(func(d *workspaceData) error { delete(d.Sessions, hash); return nil }); err != nil {
					report(w, err)
					return
				}
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
		jsonReply(w, 200, map[string]any{"member": current, "session": appToken(c, current.ID, time.Now().Add(24*time.Hour).Unix())})
	}
	mux.HandleFunc("GET /api/session", handler)
	mux.HandleFunc("DELETE /api/session", handler)
}
