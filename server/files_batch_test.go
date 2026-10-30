package main

import (
	"bytes"
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

func TestBatchFilesScannerRejectsEntireMessage(t *testing.T) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	t.Setenv("NIMBO_CLAMD_ADDRESS", listener.Addr().String())
	done := make(chan struct{})
	go func() {
		defer close(done)
		for _, verdict := range []string{"stream: OK\x00", "stream: TestSignature FOUND\x00"} {
			conn, err := listener.Accept()
			if err != nil {
				return
			}
			buf := make([]byte, 4096)
			_, _ = conn.Read(buf)
			_, _ = io.WriteString(conn, verdict)
			conn.Close()
		}
	}()
	s, err := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	if err != nil {
		t.Fatal(err)
	}
	h := newWorkspaceHandler(testConfig(), s, nil)
	owner, token, _ := appLogin(t, h, "Owner", "")
	if err = s.assignOwner(owner.ID, false); err != nil {
		t.Fatal(err)
	}
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/general/files", token, filePolicy{Enabled: true, Scan: true}), 200)
	mustStatus(t, batchFileRequest(t, h, token, randomKey(), []string{"first clean file", "second blocked file"}), 422)
	listener.Close()
	<-done
	entries, _ := os.ReadDir(filepath.Dir(s.filePath("x")))
	if len(entries) != 0 || len(s.data.Messages["general"]) != 0 {
		t.Fatal("scanner rejection published a partial batch")
	}
}

func batchFileRequest(t *testing.T, h http.Handler, token, client string, contents []string) *httptest.ResponseRecorder {
	t.Helper()
	var b bytes.Buffer
	m := multipart.NewWriter(&b)
	for _, content := range contents {
		p, _ := m.CreateFormFile("file", "sample.txt")
		_, _ = io.WriteString(p, content)
	}
	_ = m.WriteField("clientId", client)
	_ = m.WriteField("text", "Two files")
	_ = m.Close()
	r := httptest.NewRequest("POST", "/api/channels/general/files", &b)
	r.Header.Set("Content-Type", m.FormDataContentType())
	r.Header.Set("Authorization", "Bearer "+token)
	r.Header.Set("Origin", testConfig().Origin)
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	return w
}

func TestBatchFilesAtomicRetryPersistenceAndDeletion(t *testing.T) {
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
	client := randomKey()
	w := batchFileRequest(t, h, token, client, []string{"first", "second"})
	mustStatus(t, w, 200)
	var msg chatMessage
	_ = json.Unmarshal(w.Body.Bytes(), &msg)
	if len(msg.Attachments) != 2 || msg.Attachment != nil || msg.Text != "Two files" {
		t.Fatalf("invalid batch: %s", w.Body.String())
	}
	for i, a := range msg.Attachments {
		w = apiCall(h, "GET", "/api/files/"+a.ID, token, nil)
		mustStatus(t, w, 200)
		if w.Body.String() != []string{"first", "second"}[i] {
			t.Fatal("wrong download")
		}
	}
	w = batchFileRequest(t, h, token, client, []string{"first", "second"})
	mustStatus(t, w, 200)
	var retry chatMessage
	_ = json.Unmarshal(w.Body.Bytes(), &retry)
	if retry.ID != msg.ID {
		t.Fatal("duplicate retry")
	}
	for _, tc := range []struct {
		data   []string
		status int
	}{
		{[]string{"valid", ""}, 413},
		{[]string{strings.Repeat("a", int(maxFileBytes)), "b"}, 413},
		{strings.Split(strings.Repeat("x,", 10)+"x", ","), 400},
	} {
		mustStatus(t, batchFileRequest(t, h, token, randomKey(), tc.data), tc.status)
	}
	entries, _ := os.ReadDir(filepath.Dir(s.filePath("x")))
	if len(entries) != 2 || len(s.data.Messages["general"]) != 1 {
		t.Fatal("rejected or retried batch leaked files/messages")
	}
	restored, err := openWorkspace(s.path, "")
	if err != nil {
		t.Fatal(err)
	}
	if len(restored.data.Messages["general"][0].Attachments) != 2 || dataSize(&restored.data) != dataSize(&s.data) {
		t.Fatal("batch persistence/accounting")
	}
	mustStatus(t, apiCall(h, "DELETE", "/api/messages/"+msg.ID+"?channel=general", token, nil), 200)
	for _, a := range msg.Attachments {
		mustStatus(t, apiCall(h, "GET", "/api/files/"+a.ID, token, nil), 404)
	}
	entries, _ = os.ReadDir(filepath.Dir(s.filePath("x")))
	if len(entries) != 0 {
		t.Fatal("deleted batch left files")
	}
}
