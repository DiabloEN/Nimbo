package main

import (
	"archive/zip"
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Opt-in integration test against a local scanner, using an isolated store and
// httptest handler. Never sends messages to a real Nimbo workspace.
func TestRealScannerFileUpload(t *testing.T) {
	address := os.Getenv("NIMBO_TEST_REAL_CLAMD")
	if address == "" {
		t.Skip("local ClamAV integration only")
	}
	t.Setenv("NIMBO_CLAMD_ADDRESS", address)
	s, e := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	if e != nil {
		t.Fatal(e)
	}
	h := newWorkspaceHandler(testConfig(), s, nil)
	owner, token, _ := appLogin(t, h, "Scanner QA", "")
	if e = s.assignOwner(owner.ID, false); e != nil {
		t.Fatal(e)
	}
	mustStatus(t, apiCall(h, "PATCH", "/api/antivirus", token, map[string]bool{"enabled": true}), 200)
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/general/files", token, filePolicy{Enabled: true, Scan: true}), 200)
	w := fileRequest(t, h, token, "boundary.txt", strings.Repeat("ordinary file data. ", int(maxFileBytes)/20)+strings.Repeat("x", int(maxFileBytes)%20), randomKey())
	mustStatus(t, w, 200)
	var msg chatMessage
	_ = json.Unmarshal(w.Body.Bytes(), &msg)
	if msg.Attachment == nil || !msg.Attachment.Scanned {
		t.Fatal("file not scanned")
	}
	eicar := "X5O!P%@AP[4" + string(rune(92)) + "PZX54(P^)7CC)7}$" + "EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*"
	mustStatus(t, fileRequest(t, h, token, "eicar.txt", eicar, randomKey()), 422)
	// A clean first file must not publish if a later file is rejected.
	mustStatus(t, batchFileRequest(t, h, token, randomKey(), []string{"ordinary file", eicar}), 422)
	var b bytes.Buffer
	z := zip.NewWriter(&b)
	entry, _ := z.Create("oversized.txt")
	_, _ = entry.Write([]byte(strings.Repeat("x", 21*1024*1024)))
	_ = z.Close()
	mustStatus(t, fileRequest(t, h, token, "oversized.zip", b.String(), randomKey()), 422)
	if len(s.data.Messages["general"]) != 1 {
		t.Fatal("rejected attachment published")
	}
	leftovers, _ := filepath.Glob(filepath.Join(filepath.Dir(s.path), "files", ".upload-*"))
	if len(leftovers) != 0 {
		t.Fatal("temporary uploads left behind")
	}
	info, e := os.Stat(s.filePath(msg.Attachment.ID))
	if e != nil || info.Mode().Perm() != 0600 {
		t.Fatal("unsafe file mode")
	}
}

func TestArchiveLimitsWithoutExtraction(t *testing.T) {
	for _, tc := range []struct {
		name   string
		size   int
		wantOK bool
	}{{"notes.txt", 100, true}, {"large.txt", 21 * 1024 * 1024, false}, {"nested.zip", 100, false}} {
		p := filepath.Join(t.TempDir(), "test.zip")
		f, e := os.Create(p)
		if e != nil {
			t.Fatal(e)
		}
		z := zip.NewWriter(f)
		w, e := z.Create(tc.name)
		if e != nil {
			t.Fatal(e)
		}
		_, _ = w.Write([]byte(strings.Repeat("x", tc.size)))
		if e = z.Close(); e != nil {
			t.Fatal(e)
		}
		f.Close()
		if e = checkArchiveLimits(p); (e == nil) != tc.wantOK {
			t.Fatalf("%s: %v", tc.name, e)
		}
	}
}

func TestAntivirusOwnerOnlyAndGlobalPolicy(t *testing.T) {
	t.Setenv("NIMBO_CLAMD_ADDRESS", "")
	t.Setenv("NIMBO_ANTIVIRUS_MANAGER_SOCKET", "")
	s, e := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	if e != nil {
		t.Fatal(e)
	}
	h := newWorkspaceHandler(testConfig(), s, nil)
	owner, ownerToken, _ := appLogin(t, h, "Owner", "")
	_, token, _ := appLogin(t, h, "Member", "")
	if e = s.assignOwner(owner.ID, false); e != nil {
		t.Fatal(e)
	}
	for _, method := range []string{"GET", "PATCH"} {
		mustStatus(t, apiCall(h, method, "/api/antivirus", token, map[string]bool{"enabled": false}), 403)
	}
	mustStatus(t, apiCall(h, "POST", "/api/antivirus/update", token, nil), 403)
	mustStatus(t, apiCall(h, "GET", "/api/antivirus", ownerToken, nil), 200)
	mustStatus(t, apiCall(h, "POST", "/api/antivirus/update", ownerToken, nil), 503)
	mustStatus(t, apiCall(h, "PATCH", "/api/antivirus", ownerToken, map[string]bool{"enabled": true}), 503)
	mustStatus(t, apiCall(h, "PATCH", "/api/antivirus", ownerToken, map[string]bool{"enabled": false}), 200)
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/general/files", ownerToken, filePolicy{Enabled: true, Scan: true}), 200)
	if policyForFiles(&s.data, "general").Scan {
		t.Fatal("global opt-out not applied")
	}
	enabled := true
	s.data.AntivirusScan = &enabled
	if !policyForFiles(&s.data, "general").Scan {
		t.Fatal("channel could bypass global scanning")
	}
}

func TestUploadedFilesRemainPrivateData(t *testing.T) {
	t.Setenv("NIMBO_CLAMD_ADDRESS", "")
	s, e := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	if e != nil {
		t.Fatal(e)
	}
	h := newWorkspaceHandler(testConfig(), s, nil)
	owner, token, _ := appLogin(t, h, "Owner", "")
	if e = s.assignOwner(owner.ID, false); e != nil {
		t.Fatal(e)
	}
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/general/files", token, filePolicy{Enabled: true}), 200)
	mustStatus(t, fileRequest(t, h, token, "payload.exe:photo.jpg", "data", randomKey()), 400)
	mustStatus(t, fileRequest(t, h, token, "photo\u202egpj.exe", "data", randomKey()), 400)
	w := fileRequest(t, h, token, "../../payload.html", "<script>fetch('/api/workspace')</script>", randomKey())
	mustStatus(t, w, 200)
	var msg chatMessage
	_ = json.Unmarshal(w.Body.Bytes(), &msg)
	if msg.Attachment == nil {
		t.Fatal("no file")
	}
	path := s.filePath(msg.Attachment.ID)
	if strings.Contains(filepath.Base(path), "payload") || filepath.Ext(path) != "" {
		t.Fatal("original name controls storage path")
	}
	info, e := os.Stat(path)
	if e != nil {
		t.Fatal(e)
	}
	// Windows does not model Unix mode bits; assert the deployment guarantee on Linux.
	if os.PathSeparator == '/' && info.Mode().Perm() != 0600 {
		t.Fatal("attachment permissions are not 0600")
	}
	w = apiCall(h, "GET", "/api/files/"+msg.Attachment.ID, token, nil)
	mustStatus(t, w, 200)
	if w.Header().Get("Content-Type") != "application/octet-stream" || !strings.HasPrefix(w.Header().Get("Content-Disposition"), "attachment;") || w.Header().Get("X-Content-Type-Options") != "nosniff" || !strings.Contains(w.Header().Get("Content-Security-Policy"), "sandbox") {
		t.Fatal("active content can be served inline")
	}
	mustStatus(t, apiCall(h, "GET", "/api/files/"+msg.Attachment.ID, "", nil), 401)
	mustStatus(t, apiCall(h, "GET", "/files/"+msg.Attachment.ID, "", nil), 404)
	if _, e = os.Stat(filepath.Join(filepath.Dir(s.path), "payload.html")); !os.IsNotExist(e) {
		t.Fatal("path escaped file storage")
	}
}
