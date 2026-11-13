package main

import (
	"encoding/json"
	"errors"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"time"
)

// Set by the server release build; independent of desktop client versions.
var serverVersion = "development"
var releaseTag = regexp.MustCompile(`^(server-)?v[0-9]{1,6}\.[0-9]{1,6}\.[0-9]{1,6}$`)

type updateStatus struct {
	Installed  string `json:"installed"`
	Available  string `json:"available"`
	Phase      string `json:"phase"`
	Detail     string `json:"detail,omitempty"`
	CanInstall bool   `json:"canInstall"`
	CheckedAt  int64  `json:"checkedAt,omitempty"`
}

func (s *workspaceStore) registerServerUpdates(handle func(string, func(http.ResponseWriter, *http.Request, string))) {
	serve := func(w http.ResponseWriter, r *http.Request, actor string) {
		s.mu.Lock()
		err := requireManager(&s.data, actor, true)
		s.mu.Unlock()
		if err != nil {
			report(w, err)
			return
		}
		state := updateStatus{Installed: serverVersion, Phase: "unmanaged"}
		dir := os.Getenv("NIMBO_UPDATE_DIR")
		checkPath := filepath.Join(dir, "requests", "check")
		var expired bool
		if dir != "" {
			raw, err := os.ReadFile(filepath.Join(dir, "status.json"))
			if err != nil || json.Unmarshal(raw, &state) != nil {
				reject(w, 503, "Служба обновлений пока недоступна")
				return
			}
			state.Installed = serverVersion
			if marker, err := os.Stat(checkPath); err == nil {
				expired = time.Since(marker.ModTime()) >= 15*time.Second
			} else if state.Phase == "checking" {
				if info, err := os.Stat(filepath.Join(dir, "status.json")); err == nil {
					expired = time.Since(info.ModTime()) >= 15*time.Second
				}
			}
			if _, err := os.Stat(filepath.Join(dir, "requests", "install")); err == nil && state.Phase != "waiting" && state.Phase != "installing" {
				state.CanInstall, state.Phase = false, "queued"
			}
		}
		if r.URL.Path == "/api/server-update/check" {
			if dir == "" || state.Phase == "queued" || state.Phase == "installing" || state.Phase == "waiting" {
				reject(w, 409, "Служба сейчас не может проверить обновления")
				return
			}
			marker, err := os.OpenFile(checkPath, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
			if err == nil {
				err = marker.Close()
			} else if errors.Is(err, os.ErrExist) && expired {
				now := time.Now()
				err = os.Chtimes(checkPath, now, now)
			}
			if err != nil && !errors.Is(err, os.ErrExist) {
				reject(w, 503, "Не удалось запросить проверку")
				return
			}
			state.Phase, state.CanInstall, state.Detail = "checking", false, ""
		} else if dir != "" && state.Phase != "queued" && state.Phase != "installing" && state.Phase != "waiting" {
			if expired {
				state.Phase, state.CanInstall, state.Detail = "error", false, "Проверка заняла слишком много времени. Попробуйте ещё раз."
			} else if _, err := os.Stat(checkPath); err == nil {
				state.Phase, state.CanInstall, state.Detail = "checking", false, ""
			}
		}
		if r.Method == "POST" && r.URL.Path == "/api/server-update" {
			var request struct {
				Tag string `json:"tag"`
			}
			if !decodeRequest(w, r, &request) {
				return
			}
			if !state.CanInstall || !releaseTag.MatchString(request.Tag) || request.Tag != state.Available {
				reject(w, 409, "Обновите сведения о релизе")
				return
			}
			// Link a completed private file atomically: two owners/tabs cannot replace a queued choice.
			inbox := filepath.Join(dir, "requests")
			tmp, err := os.CreateTemp(inbox, ".request-")
			if err != nil {
				reject(w, 503, "Не удалось поставить обновление в очередь")
				return
			}
			defer os.Remove(tmp.Name())
			_, err = tmp.WriteString(request.Tag)
			closeErr := tmp.Close()
			if err == nil && closeErr == nil {
				err = os.Link(tmp.Name(), filepath.Join(inbox, "install"))
			}
			if errors.Is(err, os.ErrExist) {
				reject(w, 409, "Обновление уже ожидает установки")
				return
			}
			if err != nil || closeErr != nil {
				reject(w, 503, "Ошибка очереди обновлений")
				return
			}
			state.CanInstall = false
			state.Phase = "queued"
		}
		w.Header().Set("Cache-Control", "no-store")
		jsonReply(w, 200, state)
	}
	handle("GET /api/server-update", serve)
	handle("POST /api/server-update", serve)
	handle("POST /api/server-update/check", serve)
}
