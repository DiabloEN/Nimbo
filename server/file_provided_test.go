package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// Opt-in: supplied fixtures are used only in a temporary workspace, never live chat.
func TestProvidedFileThroughScanner(t *testing.T) {
	path, address := os.Getenv("NIMBO_TEST_FILE"), os.Getenv("NIMBO_TEST_REAL_CLAMD")
	if path == "" || address == "" {
		t.Skip("private fixture and scanner required")
	}
	t.Setenv("NIMBO_CLAMD_ADDRESS", address)
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	s, err := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	if err != nil {
		t.Fatal(err)
	}
	h := newWorkspaceHandler(testConfig(), s, nil)
	owner, token, _ := appLogin(t, h, "Fixture QA", "")
	_ = s.assignOwner(owner.ID, false)
	mustStatus(t, apiCall(h, "PATCH", "/api/antivirus", token, map[string]bool{"enabled": true}), 200)
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/general/files", token, filePolicy{Enabled: true, Scan: true}), 200)
	start := time.Now()
	response := fileRequest(t, h, token, "СВОЯК_Дима_6_Кокошка,_Макар,_Сатир.siq", string(b), randomKey())
	mustStatus(t, response, 200)
	var m chatMessage
	if err = json.Unmarshal(response.Body.Bytes(), &m); err != nil {
		t.Fatal(err)
	}
	if m.Attachment == nil || !m.Attachment.Scanned || m.Attachment.Size != int64(len(b)) {
		t.Fatal("attachment not scanned or size changed")
	}
	t.Logf("Provided package accepted and scanned in %s", time.Since(start).Round(time.Millisecond))
}
