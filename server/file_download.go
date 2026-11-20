package main

import (
	"net/http"
	"time"
)

// A short-lived capability for one attachment, never a profile/session token.
// It is kept only in bounded memory, revoked with its session and consumed by GET.
type fileDownloadGrant struct {
	FileID, Session, Member string
	Until                   time.Time
	Timer                   *time.Timer
}

func (s *workspaceStore) issueFileDownload(grant fileDownloadGrant, lifetime time.Duration) (string, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.downloads == nil {
		s.downloads = map[string]*fileDownloadGrant{}
	}
	count := 0
	for _, entry := range s.downloads {
		if entry.Member == grant.Member {
			count++
		}
	}
	if len(s.downloads) >= 1024 || count >= 8 {
		return "", fail(429, "Подождите завершения предыдущих скачиваний.")
	}
	ticket := randomKey()
	key := keyHash(ticket)
	grant.Until = time.Now().Add(lifetime)
	s.downloads[key] = &grant
	grant.Timer = time.AfterFunc(lifetime, func() {
		s.mu.Lock()
		defer s.mu.Unlock()
		delete(s.downloads, key)
	})
	return ticket, nil
}

func (s *workspaceStore) registerFileDownloads(mux *http.ServeMux, c config, handle func(string, func(http.ResponseWriter, *http.Request, string))) {
	handle("POST /api/files/{id}/download", func(w http.ResponseWriter, r *http.Request, actor string) {
		fileID := r.PathValue("id")
		if !validKey(fileID) {
			reject(w, 404, "Файл недоступен")
			return
		}
		s.mu.Lock()
		found := false
		for channel, messages := range s.data.Messages {
			if fileAccess(&s.data, actor, channel, false) != nil {
				continue
			}
			for _, message := range messages {
				for _, file := range messageAttachments(message) {
					if file.ID == fileID {
						found = true
					}
				}
			}
		}
		s.mu.Unlock()
		if !found {
			reject(w, 404, "Файл недоступен")
			return
		}
		ticket, err := s.issueFileDownload(fileDownloadGrant{FileID: fileID, Session: signedSessionKey(c, r), Member: actor}, 30*time.Second)
		if err != nil {
			report(w, err)
			return
		}
		w.Header().Set("Cache-Control", "no-store")
		jsonReply(w, 200, map[string]any{"ticket": ticket, "expiresIn": 30})
	})
	// Browser navigation cannot attach an Authorization header. Cookies alone
	// do not authorize this route, including for first-party requests.
	mux.HandleFunc("GET /api/download/{ticket}", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "private, no-store")
		w.Header().Set("Referrer-Policy", "no-referrer")
		if !validKey(r.PathValue("ticket")) {
			reject(w, 404, "Ссылка скачивания недействительна")
			return
		}
		key := keyHash(r.PathValue("ticket"))
		s.mu.Lock()
		grant := s.downloads[key]
		valid := grant != nil && time.Now().Before(grant.Until)
		if valid {
			session, exists := s.data.Sessions[grant.Session]
			member := findMember(&s.data, grant.Member)
			valid = exists && session.MemberID == grant.Member && session.Expires > time.Now().Unix() && member != nil && !isBlocked(member)
		}
		if grant != nil && (r.Method == "GET" || !valid) {
			delete(s.downloads, key)
			grant.Timer.Stop()
		}
		s.mu.Unlock()
		if !valid {
			reject(w, 401, "Запросите скачивание ещё раз")
			return
		}
		r.SetPathValue("id", grant.FileID)
		// Rechecks file/channel access and deletion, and streams without rescanning.
		s.downloadFile(w, r, grant.Member)
	})
}
