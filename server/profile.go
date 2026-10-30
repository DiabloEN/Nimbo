package main

import (
	"bytes"
	"encoding/base64"
	"image"
	_ "image/jpeg"
	_ "image/png"
	"net/http"
	"strings"
)

func validAvatar(value string) bool {
	if value == "" {
		return true
	}
	if len(value) > 49152 {
		return false
	}
	var format string
	if strings.HasPrefix(value, "data:image/jpeg;base64,") {
		format = "jpeg"
	} else if strings.HasPrefix(value, "data:image/png;base64,") {
		format = "png"
	} else {
		return false
	}
	raw, err := base64.StdEncoding.DecodeString(strings.SplitN(value, ",", 2)[1])
	if err != nil {
		return false
	}
	cfg, actual, err := image.DecodeConfig(bytes.NewReader(raw))
	return err == nil && actual == format && cfg.Width > 0 && cfg.Height > 0 && cfg.Width <= 256 && cfg.Height <= 256
}

func (s *workspaceStore) registerProfile(handle func(string, func(http.ResponseWriter, *http.Request, string))) {
	handle("GET /api/profile", func(w http.ResponseWriter, r *http.Request, id string) {
		s.mu.Lock()
		m := *findMember(&s.data, id)
		s.mu.Unlock()
		m.KeyHash = ""
		jsonReply(w, 200, m)
	})
	handle("GET /api/avatars/{id}", func(w http.ResponseWriter, r *http.Request, id string) {
		s.mu.Lock()
		m := findMember(&s.data, r.PathValue("id"))
		value := ""
		if m != nil {
			value = m.Avatar
		}
		s.mu.Unlock()
		if value == "" {
			http.NotFound(w, r)
			return
		}
		parts := strings.SplitN(value, ",", 2)
		data, err := base64.StdEncoding.DecodeString(parts[1])
		if err != nil {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", strings.TrimSuffix(strings.TrimPrefix(parts[0], "data:"), ";base64"))
		w.Header().Set("Cache-Control", "private, no-store")
		w.Write(data)
	})
	handle("PATCH /api/profile", func(w http.ResponseWriter, r *http.Request, id string) {
		var req struct {
			Name   string `json:"name"`
			Avatar string `json:"avatar"`
		}
		if !decodeRequest(w, r, &req) {
			return
		}
		req.Name = strings.TrimSpace(req.Name)
		if !validLabel(req.Name, 40) || !validAvatar(req.Avatar) {
			reject(w, 400, "Имя: 1–40 символов. Аватар: JPEG или PNG до 256×256 и 48 КБ.")
			return
		}
		err := s.update(func(data *workspaceData) error {
			m := findMember(data, id)
			m.Name = req.Name
			m.Avatar = req.Avatar
			m.AvatarVersion = ""
			if req.Avatar != "" {
				m.AvatarVersion = keyHash(req.Avatar)
			}
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, map[string]bool{"ok": true})
	})
}
