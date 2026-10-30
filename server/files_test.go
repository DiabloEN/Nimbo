package main

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"mime/multipart"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func fileRequest(t *testing.T, h http.Handler, token, name, content, client string) *httptest.ResponseRecorder {
	t.Helper()
	var b bytes.Buffer
	m := multipart.NewWriter(&b)
	p, _ := m.CreateFormFile("file", name)
	_, _ = io.WriteString(p, content)
	_ = m.WriteField("clientId", client)
	_ = m.Close()
	r := httptest.NewRequest("POST", "/api/channels/general/files", &b)
	r.Header.Set("Content-Type", m.FormDataContentType())
	r.Header.Set("Authorization", "Bearer "+token)
	r.Header.Set("Origin", testConfig().Origin)
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	return w
}
func TestFileUploadAuthorizationQuotaAndDeletion(t *testing.T) {
	t.Setenv("NIMBO_CLAMD_ADDRESS", "")
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
	endpoint := "/api/channels/general/files"
	mustStatus(t, fileRequest(t, h, token, "a.txt", "hello", randomKey()), 403)
	mustStatus(t, apiCall(h, "PATCH", endpoint, token, filePolicy{Enabled: true}), 403)
	mustStatus(t, apiCall(h, "PATCH", endpoint, ownerToken, filePolicy{Enabled: true, Scan: true}), 409)
	mustStatus(t, apiCall(h, "PATCH", endpoint, ownerToken, filePolicy{Enabled: true, Scan: false}), 200)
	client := randomKey()
	w := fileRequest(t, h, token, "../../unsafe.html", "<script>alert(1)</script>", client)
	mustStatus(t, w, 200)
	var msg chatMessage
	_ = json.Unmarshal(w.Body.Bytes(), &msg)
	if msg.Attachment == nil || msg.Attachment.Scanned || msg.Attachment.Name != "unsafe.html" {
		t.Fatalf("bad attachment: %s", w.Body.String())
	}
	w = fileRequest(t, h, token, "a.txt", "retry", client)
	mustStatus(t, w, 200)
	var retry chatMessage
	_ = json.Unmarshal(w.Body.Bytes(), &retry)
	if retry.ID != msg.ID {
		t.Fatal("upload retry duplicated message")
	}
	files, _ := os.ReadDir(filepath.Dir(s.filePath(msg.Attachment.ID)))
	if len(files) != 1 {
		t.Fatal("upload leaked temporary/duplicate files")
	}
	path := "/api/files/" + msg.Attachment.ID
	mustStatus(t, apiCall(h, "GET", path, "", nil), 401)
	w = apiCall(h, "GET", path, token, nil)
	mustStatus(t, w, 200)
	if w.Header().Get("Content-Type") != "application/octet-stream" || !strings.HasPrefix(w.Header().Get("Content-Disposition"), "attachment;") || w.Header().Get("X-Content-Type-Options") != "nosniff" {
		t.Fatal("unsafe download headers")
	}
	mustStatus(t, fileRequest(t, h, token, "big.bin", strings.Repeat("a", int(maxFileBytes+1)), randomKey()), 413)
	// The exact advertised boundary is accepted and participates in storage accounting.
	w = fileRequest(t, h, token, "boundary.bin", strings.Repeat("b", int(maxFileBytes)), randomKey())
	mustStatus(t, w, 200)
	var boundary chatMessage
	_ = json.Unmarshal(w.Body.Bytes(), &boundary)
	if boundary.Attachment == nil || boundary.Attachment.Size != maxFileBytes || dataSize(&s.data) < maxFileBytes {
		t.Fatal("20 MiB boundary or storage accounting is incorrect")
	}
	// Simulate a quota just above current use: a rejected upload must not evict history.
	s.mu.Lock()
	s.data.Storage.LimitBytes = (dataSize(&s.data) + 70000) * 10 / 9
	before := len(s.data.Messages["general"])
	s.mu.Unlock()
	mustStatus(t, fileRequest(t, h, token, "quota.bin", strings.Repeat("c", 100000), randomKey()), 507)
	if len(s.data.Messages["general"]) != before {
		t.Fatal("rejected upload removed messages")
	}
	// A configured but unavailable scanner must not publish a file.
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	address := listener.Addr().String()
	listener.Close()
	t.Setenv("NIMBO_CLAMD_ADDRESS", address)
	mustStatus(t, apiCall(h, "PATCH", endpoint, ownerToken, filePolicy{Enabled: true, Scan: true}), 200)
	mustStatus(t, fileRequest(t, h, token, "unchecked.txt", "harmless", randomKey()), 503)
	if len(s.data.Messages["general"]) != before {
		t.Fatal("unscanned upload was published")
	}
	mustStatus(t, apiCall(h, "DELETE", "/api/messages/"+msg.ID+"?channel=general", token, nil), 200)
	mustStatus(t, apiCall(h, "GET", path, token, nil), 404)
	if _, e = os.Stat(s.filePath(msg.Attachment.ID)); !os.IsNotExist(e) {
		t.Fatal("deleted file remains")
	}
}
func TestFileScanOnlyAcceptsExplicitCleanVerdict(t *testing.T) {
	path := filepath.Join(t.TempDir(), "file")
	_ = os.WriteFile(path, []byte("harmless test content"), 0600)
	for _, tc := range []struct {
		response string
		ok       bool
	}{{"stream: OK\x00", true}, {"stream: TestSignature FOUND\x00", false}, {"stream: size limit ERROR\x00", false}, {"", false}} {
		listener, e := net.Listen("tcp", "127.0.0.1:0")
		if e != nil {
			t.Fatal(e)
		}
		t.Setenv("NIMBO_CLAMD_ADDRESS", listener.Addr().String())
		done := make(chan struct{})
		go func() {
			defer close(done)
			conn, e := listener.Accept()
			if e != nil {
				return
			}
			defer conn.Close()
			buf := make([]byte, 4096)
			_, _ = conn.Read(buf)
			_, _ = io.WriteString(conn, tc.response)
		}()
		err := scanFile(context.Background(), path)
		listener.Close()
		<-done
		if (err == nil) != tc.ok {
			t.Fatalf("verdict %q: %v", tc.response, err)
		}
	}
	t.Setenv("NIMBO_CLAMD_ADDRESS", "192.0.2.1:3310")
	if scanFile(context.Background(), path) == nil {
		t.Fatal("accepted remote scanner")
	}
}
