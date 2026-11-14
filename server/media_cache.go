package main

import (
	"net/http"
	"time"
)

// A fresh random revision after restart makes every reconnect reconcile its disk index.
func removedChatContent(before, after *workspaceData) bool {
	for ch, old := range before.Messages {
		if len(old) == 0 {
			continue
		}
		if len(after.Messages[ch]) < len(old) || (findChannel(before, ch) != nil && findChannel(after, ch) == nil) {
			return true
		}
		// A new post may also prune an older message in the same transaction.
		if len(after.Messages[ch]) > 0 && old[0].ID != after.Messages[ch][0].ID {
			return true
		}
	}
	return false
}

func (s *workspaceStore) syncMediaCache(w http.ResponseWriter, r *http.Request, actor string) {
	var req struct {
		Cursor string   `json:"cursor"`
		IDs    []string `json:"ids"`
	}
	if !decodeRequest(w, r, &req) {
		return
	}
	if len(req.IDs) > 256 || len(req.Cursor) > 64 {
		reject(w, 400, "Некорректный запрос")
		return
	}
	s.mu.Lock()
	if s.mediaRevision == "" {
		s.mediaRevision = randomKey()
	}
	if s.mediaChanged == nil {
		s.mediaChanged = make(chan struct{})
	}
	wait := s.mediaChanged
	if req.Cursor == s.mediaRevision && req.IDs == nil {
		s.mu.Unlock()
		timer := time.NewTimer(8 * time.Second)
		defer timer.Stop()
		select {
		case <-wait:
		case <-timer.C:
		case <-r.Context().Done():
			return
		}
		s.mu.Lock()
	}
	m := findMember(&s.data, actor)
	if m == nil || isBlocked(m) {
		s.mu.Unlock()
		reject(w, 401, "Войдите в Nimbo заново")
		return
	}
	missing := make(map[string]bool, len(req.IDs))
	for _, id := range req.IDs {
		missing[id] = true
	}
	if len(missing) > 0 {
		for ch, rows := range s.data.Messages {
			if fileAccess(&s.data, actor, ch, false) != nil {
				continue
			}
			for _, row := range rows {
				for _, f := range messageAttachments(row) {
					delete(missing, f.ID)
				}
			}
		}
	}
	ids := make([]string, 0, len(missing))
	for id := range missing {
		ids = append(ids, id)
	}
	revision := s.mediaRevision
	s.mu.Unlock()
	jsonReply(w, 200, map[string]any{"cursor": revision, "missing": ids})
}
