package main

import (
	"net/http"
	"time"
)

// Each authorised tab holds one HTTP request. The browser renews it on
// completion, independent of background-tab timer throttling. Even a silent
// network partition expires without relying on the OS TCP timeout.
const webTabInterval = 10 * time.Second
const webTabRenewalGrace = 4 * time.Second

type webSessionTabs struct {
	count      int
	renewUntil time.Time
	grace      *time.Timer
}

// Ephemeral, protected by workspaceStore.mu. Keys are verified session hashes.
type webPresence struct {
	sessions map[string]*webSessionTabs
	total    int
}

func (s *workspaceStore) openWebTab(session string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	tabs := s.webTabs.sessions[session]
	if s.webTabs.total >= 2000 || (tabs == nil && len(s.webTabs.sessions) >= 2000) || (tabs != nil && tabs.count >= 16) {
		return false
	}
	if tabs == nil {
		tabs = &webSessionTabs{}
		if s.webTabs.sessions == nil {
			s.webTabs.sessions = make(map[string]*webSessionTabs)
		}
		s.webTabs.sessions[session] = tabs
	}
	tabs.count++
	s.webTabs.total++
	return true
}

func (s *workspaceStore) closeWebTab(session string, renewing bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	tabs := s.webTabs.sessions[session]
	tabs.count--
	s.webTabs.total--
	if renewing {
		tabs.renewUntil = time.Now().Add(webTabRenewalGrace)
		if tabs.grace != nil {
			tabs.grace.Reset(webTabRenewalGrace)
		} else {
			tabs.grace = time.AfterFunc(webTabRenewalGrace, func() {
				s.mu.Lock()
				defer s.mu.Unlock()
				if s.webTabs.sessions[session] == tabs && tabs.count == 0 && !time.Now().Before(tabs.renewUntil) {
					delete(s.webTabs.sessions, session)
				}
			})
		}
		return
	}
	if tabs.count == 0 && !time.Now().Before(tabs.renewUntil) {
		if tabs.grace != nil {
			tabs.grace.Stop()
		}
		delete(s.webTabs.sessions, session)
	}
}

func (s *workspaceStore) onlineMembersLocked(now time.Time) []string {
	unique := make(map[string]bool)
	for session, tabs := range s.webTabs.sessions {
		entry := s.data.Sessions[session]
		if entry.Expires > now.Unix() && (tabs.count > 0 || now.Before(tabs.renewUntil)) {
			unique[entry.MemberID] = true
		}
	}
	online := []string{}
	for _, m := range s.data.Members {
		if unique[m.ID] && !isBlocked(&m) {
			online = append(online, m.ID)
		}
	}
	return online
}

func (s *workspaceStore) watchWebTab(w http.ResponseWriter, r *http.Request, session string) {
	w.Header().Set("Cache-Control", "no-store")
	if !s.openWebTab(session) {
		reject(w, 429, "Слишком много открытых вкладок")
		return
	}
	timer := time.NewTimer(webTabInterval)
	defer timer.Stop()
	select {
	case <-r.Context().Done():
		s.closeWebTab(session, false)
	case <-timer.C:
		s.closeWebTab(session, true)
		w.WriteHeader(http.StatusNoContent)
	}
}
