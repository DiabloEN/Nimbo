package main

import (
	"bytes"
	"encoding/binary"
	"image"
	_ "image/gif"
	_ "image/jpeg"
	_ "image/png"
	"io"
	"mime"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// Reuse the existing HttpOnly /api/session cookie. No credentials in media URLs,
// no public files: every range request checks the live session and membership.
func (s *workspaceStore) registerFileMedia(mux *http.ServeMux, c config) {
	mux.HandleFunc("GET /api/session/media/{id}", func(w http.ResponseWriter, r *http.Request) {
		if origin := r.Header.Get("Origin"); origin != "" && origin != c.Origin {
			reject(w, 403, "Недопустимый источник запроса")
			return
		}
		if site := r.Header.Get("Sec-Fetch-Site"); site != "" && site != "same-origin" && site != "none" {
			reject(w, 403, "Недопустимый источник запроса")
			return
		}
		cookie, err := r.Cookie(rememberCookieName())
		if err != nil || !validKey(cookie.Value) {
			reject(w, 401, "Войдите в Nimbo")
			return
		}
		id := r.PathValue("id")
		if !validKey(id) {
			reject(w, 404, "Файл недоступен")
			return
		}
		s.mu.Lock()
		session, ok := s.data.Sessions[keyHash(cookie.Value)]
		member := findMember(&s.data, session.MemberID)
		if !ok || session.Expires <= time.Now().Unix() || member == nil || isBlocked(member) {
			s.mu.Unlock()
			reject(w, 401, "Войдите в Nimbo")
			return
		}
		var found *fileAttachment
		for ch, ms := range s.data.Messages {
			if fileAccess(&s.data, member.ID, ch, false) != nil {
				continue
			}
			for _, m := range ms {
				for _, a := range messageAttachments(m) {
					if a.ID == id {
						value := a
						found = &value
					}
				}
			}
		}
		if found == nil {
			s.mu.Unlock()
			reject(w, 404, "Файл недоступен")
			return
		}
		f, err := os.Open(s.filePath(id))
		s.mu.Unlock()
		if err != nil {
			reject(w, 404, "Файл недоступен")
			return
		}
		defer f.Close()
		info, err := f.Stat()
		if err != nil || info.Size() > maxFileBytes || info.Size() <= 0 {
			reject(w, 415, "Предпросмотр недоступен")
			return
		}
		header := make([]byte, 65536)
		n, _ := f.ReadAt(header, 0)
		mediaType := safeMediaType(found.Name, header[:n])
		if mediaType == "" {
			reject(w, 415, "Предпросмотр недоступен")
			return
		}
		w.Header().Set("Content-Type", mediaType)
		w.Header().Set("Content-Disposition", mime.FormatMediaType("inline", map[string]string{"filename": found.Name}))
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("Content-Security-Policy", "sandbox; default-src 'none'")
		w.Header().Set("Cross-Origin-Resource-Policy", "same-origin")
		w.Header().Set("Referrer-Policy", "no-referrer")
		w.Header().Set("Cache-Control", "private, no-store")
		_ = http.NewResponseController(w).SetWriteDeadline(time.Now().Add(120 * time.Second))
		http.ServeContent(w, r, found.Name, time.Time{}, f)
	})
}

// Only passive formats with matching signatures; image dimensions are bounded
// before the browser decodes. Never render SVG, HTML, PDF or unknown content.
func safeMediaType(name string, b []byte) string {
	ext := strings.ToLower(filepath.Ext(name))
	dimensions := func(w, h int) bool { return w > 0 && h > 0 && w <= 8192 && h <= 8192 && int64(w)*int64(h) <= 24000000 }
	switch ext {
	case ".png", ".jpg", ".jpeg", ".gif":
		cfg, format, err := image.DecodeConfig(io.LimitReader(bytes.NewReader(b), 65536))
		if err != nil || !dimensions(cfg.Width, cfg.Height) {
			return ""
		}
		if (ext == ".png" && format == "png") || ((ext == ".jpg" || ext == ".jpeg") && format == "jpeg") || (ext == ".gif" && format == "gif") {
			return "image/" + format
		}
	case ".webp":
		if len(b) < 30 || string(b[:4]) != "RIFF" || string(b[8:12]) != "WEBP" {
			return ""
		}
		var w, h int
		switch string(b[12:16]) {
		case "VP8X":
			w = 1 + int(b[24]) + int(b[25])<<8 + int(b[26])<<16
			h = 1 + int(b[27]) + int(b[28])<<8 + int(b[29])<<16
		case "VP8 ":
			if b[23] != 157 || b[24] != 1 || b[25] != 42 {
				return ""
			}
			w = int(binary.LittleEndian.Uint16(b[26:28]) & 16383)
			h = int(binary.LittleEndian.Uint16(b[28:30]) & 16383)
		case "VP8L":
			if b[20] != 47 {
				return ""
			}
			w = 1 + int(b[21]) + int(b[22]&63)<<8
			h = 1 + int(b[22]>>6) + int(b[23])<<2 + int(b[24]&15)<<10
		}
		if dimensions(w, h) {
			return "image/webp"
		}
	case ".mp3":
		if bytes.HasPrefix(b, []byte("ID3")) || (len(b) >= 2 && b[0] == 255 && b[1]&224 == 224) {
			return "audio/mpeg"
		}
	case ".wav":
		if len(b) >= 12 && string(b[:4]) == "RIFF" && string(b[8:12]) == "WAVE" {
			return "audio/wav"
		}
	case ".ogg", ".oga", ".ogv":
		if bytes.HasPrefix(b, []byte("OggS")) {
			if ext == ".ogv" {
				return "video/ogg"
			}
			return "audio/ogg"
		}
	case ".flac":
		if bytes.HasPrefix(b, []byte("fLaC")) {
			return "audio/flac"
		}
	case ".mp4", ".m4a":
		if len(b) >= 12 && string(b[4:8]) == "ftyp" {
			if ext == ".m4a" {
				return "audio/mp4"
			}
			return "video/mp4"
		}
	case ".webm":
		if bytes.HasPrefix(b, []byte{26, 69, 223, 163}) {
			return "video/webm"
		}
	}
	return ""
}
