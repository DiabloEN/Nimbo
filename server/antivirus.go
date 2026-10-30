package main

import (
	"bufio"
	"context"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

func antivirusVersion(ctx context.Context) (string, error) {
	address := os.Getenv("NIMBO_CLAMD_ADDRESS")
	network := "tcp"
	if strings.HasPrefix(address, "unix:") {
		network = "unix"
		address = strings.TrimPrefix(address, "unix:")
	} else {
		host, _, err := net.SplitHostPort(address)
		if err != nil || !net.ParseIP(host).IsLoopback() {
			return "", fail(503, "ClamAV не настроен")
		}
	}
	conn, err := (&net.Dialer{Timeout: 2 * time.Second}).DialContext(ctx, network, address)
	if err != nil {
		return "", err
	}
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(2 * time.Second))
	if _, err = io.WriteString(conn, "zVERSION\x00"); err != nil {
		return "", err
	}
	v, err := bufio.NewReader(io.LimitReader(conn, 512)).ReadString(0)
	if err != nil || !strings.HasPrefix(v, "ClamAV ") {
		return "", fail(503, "ClamAV не ответил")
	}
	return strings.TrimSuffix(v, "\x00"), nil
}

// The optional updater accepts only status/update over a private Unix socket.
// It runs as clamav, cannot install software, and receives no user file paths.
func antivirusManager(ctx context.Context, method string) (map[string]any, error) {
	socket := os.Getenv("NIMBO_ANTIVIRUS_MANAGER_SOCKET")
	if socket == "" || !filepath.IsAbs(socket) {
		return nil, fail(503, "Обновление баз на хосте не настроено")
	}
	transport := &http.Transport{DialContext: func(ctx context.Context, _, _ string) (net.Conn, error) {
		return (&net.Dialer{Timeout: 2 * time.Second}).DialContext(ctx, "unix", socket)
	}}
	defer transport.CloseIdleConnections()
	path := "status"
	if method == "POST" {
		path = "update"
	}
	req, err := http.NewRequestWithContext(ctx, method, "http://localhost/"+path, nil)
	if err != nil {
		return nil, err
	}
	resp, err := (&http.Client{Transport: transport, Timeout: 4 * time.Second}).Do(req)
	if err != nil {
		return nil, fail(503, "Служба обновления баз недоступна")
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return nil, fail(503, "Не удалось запустить обновление баз")
	}
	var data map[string]any
	err = json.NewDecoder(io.LimitReader(resp.Body, 8192)).Decode(&data)
	return data, err
}

func (s *workspaceStore) registerAntivirus(handle func(string, func(http.ResponseWriter, *http.Request, string))) {
	owner := func(actor string) error {
		s.mu.Lock()
		defer s.mu.Unlock()
		return requireManager(&s.data, actor, true)
	}
	handle("GET /api/antivirus", func(w http.ResponseWriter, r *http.Request, actor string) {
		if err := owner(actor); err != nil {
			report(w, err)
			return
		}
		s.mu.Lock()
		enabled := true
		if s.data.AntivirusScan != nil {
			enabled = *s.data.AntivirusScan
		}
		s.mu.Unlock()
		version, err := antivirusVersion(r.Context())
		data := map[string]any{"enabled": enabled, "name": "ClamAV", "configured": os.Getenv("NIMBO_CLAMD_ADDRESS") != "", "available": err == nil, "version": version, "updateConfigured": os.Getenv("NIMBO_ANTIVIRUS_MANAGER_SOCKET") != ""}
		if updater, e := antivirusManager(r.Context(), "GET"); e == nil {
			data["updater"] = updater
		}
		jsonReply(w, 200, data)
	})
	handle("PATCH /api/antivirus", func(w http.ResponseWriter, r *http.Request, actor string) {
		if err := owner(actor); err != nil {
			report(w, err)
			return
		}
		var p struct {
			Enabled *bool `json:"enabled"`
		}
		if !decodeRequest(w, r, &p) {
			return
		}
		if p.Enabled == nil {
			reject(w, 400, "Выберите режим проверки")
			return
		}
		if *p.Enabled {
			if _, err := antivirusVersion(r.Context()); err != nil {
				reject(w, 503, "ClamAV пока недоступен. Настройки не изменены.")
				return
			}
		}
		err := s.update(func(d *workspaceData) error {
			if e := requireManager(d, actor, true); e != nil {
				return e
			}
			d.AntivirusScan = p.Enabled
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, p)
	})
	handle("POST /api/antivirus/update", func(w http.ResponseWriter, r *http.Request, actor string) {
		if err := owner(actor); err != nil {
			report(w, err)
			return
		}
		data, err := antivirusManager(r.Context(), "POST")
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, data)
	})
}
