package main

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"image"
	"image/png"
	"path/filepath"
	"testing"
)

func TestWorkspaceAvatarAuthorizationValidationPersistence(t *testing.T) {
	path := filepath.Join(t.TempDir(), "workspace.json")
	s, _ := openWorkspace(path, "")
	h := newWorkspaceHandler(testConfig(), s, nil)
	m, admin, _ := appLogin(t, h, "Admin", "")
	_, guest, _ := appLogin(t, h, "Guest", "")
	s.update(func(d *workspaceData) error { findMember(d, m.ID).Role = "admin"; return nil })
	var b bytes.Buffer
	png.Encode(&b, image.NewRGBA(image.Rect(0, 0, 4, 4)))
	avatar := "data:image/png;base64," + base64.StdEncoding.EncodeToString(b.Bytes())
	body := map[string]string{"name": "Тест", "avatar": avatar}
	mustStatus(t, apiCall(h, "PATCH", "/api/workspace", guest, body), 403)
	mustStatus(t, apiCall(h, "PATCH", "/api/workspace", admin, body), 200)
	var result map[string]any
	json.Unmarshal(apiCall(h, "GET", "/api/workspace", guest, nil).Body.Bytes(), &result)
	if result["avatar"] != avatar {
		t.Fatal("avatar unavailable to members")
	}
	saved, err := openWorkspace(path, "")
	if err != nil || saved.data.Avatar != avatar {
		t.Fatal("avatar not persisted")
	}
	body["avatar"] = "data:image/svg+xml;base64,PHN2Zz4="
	mustStatus(t, apiCall(h, "PATCH", "/api/workspace", admin, body), 400)
	if s.data.Avatar != avatar {
		t.Fatal("invalid image changed avatar")
	}
	mustStatus(t, apiCall(h, "PATCH", "/api/workspace", admin, map[string]string{"name": "New"}), 200)
	if s.data.Avatar != avatar {
		t.Fatal("rename erased avatar")
	}
	body["avatar"] = ""
	mustStatus(t, apiCall(h, "PATCH", "/api/workspace", admin, body), 200)
	if s.data.Avatar != "" {
		t.Fatal("remove failed")
	}
}
