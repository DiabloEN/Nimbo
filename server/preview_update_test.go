package main

import (
	"encoding/json"
	"net/netip"
	"net/url"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestPreviewCards(t *testing.T) {
	card, img := parsePreview([]byte(`<meta property="og:title" content="Fallback"><meta property="og:image" content="/p.jpg">
<script type="application/ld+json">{"@graph":[{"@type":["Product"],"name":"Чай","offers":{"price":150,"priceCurrency":"RUB"}}]}</script>`))
	if card.Title != "Чай" || card.Price != "150 RUB" || img != "/p.jpg" {
		t.Fatal(card, img)
	}
	card, _ = parsePreview([]byte(`<meta property="og:title" content="A &amp; B">`))
	if card.Title != "A & B" {
		t.Fatal(card)
	}
	for _, ip := range []string{"127.0.0.1", "169.254.169.254", "192.168.1.1", "100.64.1.1", "::1", "198.18.0.1"} {
		if publicPreviewIP(netip.MustParseAddr(ip)) {
			t.Fatal(ip)
		}
	}
	for _, raw := range []string{"https://youtube.com.evil.test/watch?v=aaaaaaaaaaa", "https://youtu.be/short"} {
		u, _ := url.Parse(raw)
		if videoID(u) != "" {
			t.Fatal(raw)
		}
	}
}

func TestUpdateQueueOwnerAndVersion(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("NIMBO_UPDATE_DIR", dir)
	if err := os.Mkdir(filepath.Join(dir, "requests"), 0700); err != nil {
		t.Fatal(err)
	}
	data, _ := json.Marshal(updateStatus{Phase: "available", Available: "server-v1.2.3", CanInstall: true})
	if err := os.WriteFile(filepath.Join(dir, "status.json"), data, 0600); err != nil {
		t.Fatal(err)
	}
	store, err := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	if err != nil {
		t.Fatal(err)
	}
	h := newWorkspaceHandler(testConfig(), store, nil)
	owner, session, _ := appLogin(t, h, "Owner", "")
	_, guest, _ := appLogin(t, h, "Guest", "")
	if err := store.assignOwner(owner.ID, false); err != nil {
		t.Fatal(err)
	}
	const path = "/api/server-update"
	body := map[string]string{"tag": "server-v1.2.3"}
	mustStatus(t, apiCall(h, "POST", path+"/check", guest, nil), 403)
	for i := 0; i < 2; i++ {
		mustStatus(t, apiCall(h, "POST", path+"/check", session, nil), 200)
	}
	if _, err := os.Stat(filepath.Join(dir, "requests", "install")); !os.IsNotExist(err) {
		t.Fatal("checking must not enqueue installation")
	}
	reply := apiCall(h, "GET", path, session, nil)
	var state updateStatus
	if err := json.Unmarshal(reply.Body.Bytes(), &state); err != nil || state.Phase != "checking" || state.CanInstall {
		t.Fatal(state, err)
	}
	marker := filepath.Join(dir, "requests", "check")
	old := time.Now().Add(-time.Minute)
	if err := os.Chtimes(marker, old, old); err != nil {
		t.Fatal(err)
	}
	reply = apiCall(h, "GET", path, session, nil)
	if err := json.Unmarshal(reply.Body.Bytes(), &state); err != nil || state.Phase != "error" || state.CanInstall {
		t.Fatal("expired check", state, err)
	}
	mustStatus(t, apiCall(h, "POST", path+"/check", session, nil), 200)
	reply = apiCall(h, "GET", path, session, nil)
	if err := json.Unmarshal(reply.Body.Bytes(), &state); err != nil || state.Phase != "checking" {
		t.Fatal("retry", state, err)
	}
	if err := os.Remove(filepath.Join(dir, "requests", "check")); err != nil {
		t.Fatal(err)
	}
	mustStatus(t, apiCall(h, "GET", path, guest, nil), 403)
	mustStatus(t, apiCall(h, "POST", path, guest, body), 403)
	mustStatus(t, apiCall(h, "GET", path, session, nil), 200)
	mustStatus(t, apiCall(h, "POST", path, session, map[string]string{"tag": "server-v1.2.2"}), 409)
	mustStatus(t, apiCall(h, "POST", path, session, body), 200)
	mustStatus(t, apiCall(h, "POST", path+"/check", session, nil), 409)
	mustStatus(t, apiCall(h, "POST", path, session, body), 409)
	tag, err := os.ReadFile(filepath.Join(dir, "requests", "install"))
	if err != nil || string(tag) != body["tag"] {
		t.Fatal(string(tag), err)
	}
}
