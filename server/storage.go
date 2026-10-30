package main

import (
	"encoding/json"
	"net/http"
	"path/filepath"
	"sort"
	"strings"
)

const mib = int64(1024 * 1024)

type storagePolicy struct {
	LimitBytes  int64 `json:"limitBytes"`
	AutoCleanup bool  `json:"autoCleanup"`
}

func (s *workspaceStore) policyFor(d *workspaceData) storagePolicy {
	p := d.Storage
	if p.LimitBytes == 0 {
		if _, total, err := diskSpace(filepath.Dir(s.path)); err == nil && s.path != "" {
			p.LimitBytes = int64(total)
		} else {
			p.LimitBytes = 256 * mib
		}
	}
	p.AutoCleanup = true
	return p
}
func metadataSize(d *workspaceData) int64 {
	b, _ := json.Marshal(d)
	return int64(len(b) + 1)
}
func dataSize(d *workspaceData) int64 {
	size := metadataSize(d)
	for _, a := range attachmentsOf(d) {
		size += a.Size
	}
	return size
}

// Logical stored bytes, matching dataSize. Classification is informational only;
// media access and validation remain independent of the file's extension.
func storageBreakdown(d *workspaceData) map[string]int64 {
	result := map[string]int64{"messages": 0, "photos": 0, "video": 0, "audio": 0, "files": 0, "other": 0}
	for _, messages := range d.Messages {
		for _, message := range messages {
			b, _ := json.Marshal(message)
			result["messages"] += int64(len(b))
		}
	}
	result["other"] = metadataSize(d) - result["messages"]
	for _, file := range attachmentsOf(d) {
		category := "files"
		switch strings.ToLower(filepath.Ext(file.Name)) {
		case ".png", ".jpg", ".jpeg", ".gif", ".webp", ".avif", ".heic", ".bmp", ".tif", ".tiff":
			category = "photos"
		case ".mp4", ".webm", ".ogv", ".mov", ".mkv", ".avi":
			category = "video"
		case ".mp3", ".wav", ".ogg", ".oga", ".flac", ".m4a", ".aac", ".opus":
			category = "audio"
		}
		result[category] += file.Size
	}
	return result
}
func trimMessages(d *workspaceData, target int64) {
	type item struct {
		channel, id string
		at          int64
		size        int64
	}
	items := []item{}
	size := dataSize(d)
	for channel, ms := range d.Messages {
		for _, m := range ms {
			b, _ := json.Marshal(m)
			size := int64(len(b) + 1)
			for _, a := range messageAttachments(m) {
				size += a.Size
			}
			items = append(items, item{channel, m.ID, m.CreatedAt, size})
		}
	}
	sort.Slice(items, func(i, j int) bool {
		if items[i].at == items[j].at {
			return items[i].id < items[j].id
		}
		return items[i].at < items[j].at
	})
	removed := map[string]bool{}
	for _, v := range items {
		if size <= target {
			break
		}
		removed[v.id] = true
		size -= v.size
	}
	for ch, ms := range d.Messages {
		out := make([]chatMessage, 0, len(ms))
		for _, m := range ms {
			if !removed[m.ID] {
				out = append(out, m)
			}
		}
		d.Messages[ch] = out
	}
}
func (s *workspaceStore) enforceStorage(next *workspaceData) error {
	policy := s.policyFor(next)
	next.Storage.AutoCleanup = true
	size := dataSize(next)
	if size > policy.LimitBytes*9/10 {
		trimMessages(next, policy.LimitBytes*9/10)
		size = dataSize(next)
	}
	// Shrinking writes (deletion/cleanup) remain possible when already over quota.
	if size > policy.LimitBytes && size >= dataSize(&s.data) {
		return fail(507, "Лимит хранилища сервера исчерпан. Владелец может очистить историю или увеличить лимит.")
	}
	if s.path != "" {
		free, _, err := diskSpace(filepath.Dir(s.path))
		if err == nil && free < uint64(metadataSize(next)+64*mib) && size >= dataSize(&s.data) {
			return fail(507, "На диске сервера мало свободного места. Новые данные временно не принимаются.")
		}
	}
	return nil
}
func (s *workspaceStore) storageStatus(w http.ResponseWriter, r *http.Request, actor string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if m := findMember(&s.data, actor); m == nil || m.Role != "owner" {
		reject(w, 403, "Настройки хранилища доступны только владельцу")
		return
	}
	count := 0
	for _, ms := range s.data.Messages {
		count += len(ms)
	}
	free, total, err := diskSpace(filepath.Dir(s.path))
	var disk any
	if err == nil && s.path != "" {
		disk = map[string]uint64{"freeBytes": free, "totalBytes": total}
	}
	jsonReply(w, 200, map[string]any{"policy": s.policyFor(&s.data), "usedBytes": dataSize(&s.data), "breakdown": storageBreakdown(&s.data), "messages": count, "disk": disk})
}
func (s *workspaceStore) registerStorage(handle func(string, func(http.ResponseWriter, *http.Request, string))) {
	handle("GET /api/storage", s.storageStatus)
	handle("PATCH /api/storage", func(w http.ResponseWriter, r *http.Request, actor string) {
		var req storagePolicy
		if !decodeRequest(w, r, &req) {
			return
		}
		if req.LimitBytes < 8*mib || req.LimitBytes > 9007199254740991 {
			reject(w, 400, "Минимальный лимит — 8 МБ")
			return
		}
		err := s.update(func(d *workspaceData) error {
			if m := findMember(d, actor); m == nil || m.Role != "owner" {
				return fail(403, "Только владелец может менять хранилище")
			}
			if _, total, err := diskSpace(filepath.Dir(s.path)); err == nil && s.path != "" && uint64(req.LimitBytes) > total {
				return fail(400, "Лимит не может превышать объём диска")
			}
			req.AutoCleanup = true
			d.Storage = req
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		s.storageStatus(w, r, actor)
	})
}
