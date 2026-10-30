package main

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"image"
	"image/color"
	"image/png"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
)

func TestKickRemovesOnlyTargetConnections(t *testing.T) {
	removed := []string{}
	api := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var req map[string]string
		json.NewDecoder(r.Body).Decode(&req)
		switch r.URL.Path {
		case "/twirp/livekit.RoomService/ListRooms":
			jsonReply(w, 200, map[string]any{"rooms": []map[string]string{{"name": "lounge"}}})
		case "/twirp/livekit.RoomService/ListParticipants":
			jsonReply(w, 200, map[string]any{"participants": []map[string]string{{"identity": "target.one"}, {"identity": "target.two"}, {"identity": "someone.else"}}})
		case "/twirp/livekit.RoomService/RemoveParticipant":
			removed = append(removed, req["identity"])
			jsonReply(w, 200, map[string]bool{})
		default:
			http.NotFound(w, r)
		}
	}))
	defer api.Close()
	p := newPresence(testConfig(), nil, api.URL)
	if err := p.disconnectMember(context.Background(), "target"); err != nil {
		t.Fatal(err)
	}
	if len(removed) != 2 || removed[0] != "target.one" || removed[1] != "target.two" {
		t.Fatalf("wrong participants: %v", removed)
	}
}

func TestProfileAndModeration(t *testing.T) {
	path := filepath.Join(t.TempDir(), "workspace.json")
	key := randomKey()
	store, _ := openWorkspace(path, key)
	h := newWorkspaceHandler(testConfig(), store, nil)
	owner, ownerToken, _ := appLogin(t, h, "Owner", "")
	guest, guestToken, guestKey := appLogin(t, h, "Guest", "")
	if err := store.assignOwner(owner.ID, false); err != nil {
		t.Fatal(err)
	}
	img := image.NewRGBA(image.Rect(0, 0, 16, 16))
	img.Set(0, 0, color.White)
	var b bytes.Buffer
	png.Encode(&b, img)
	avatar := "data:image/png;base64," + base64.StdEncoding.EncodeToString(b.Bytes())
	mustStatus(t, apiCall(h, "PATCH", "/api/profile", guestToken, map[string]string{"name": "Renamed", "avatar": avatar, "role": "owner"}), 400)
	mustStatus(t, apiCall(h, "PATCH", "/api/profile", guestToken, map[string]string{"name": "Renamed", "avatar": avatar}), 200)
	mustStatus(t, apiCall(h, "GET", "/api/avatars/"+guest.ID, "", nil), 401)
	w := apiCall(h, "GET", "/api/workspace", ownerToken, nil)
	if bytes.Contains(w.Body.Bytes(), []byte(avatar)) {
		t.Fatal("poll duplicates avatar bytes")
	}
	mustStatus(t, apiCall(h, "POST", "/api/members/"+owner.ID+"/moderate", guestToken, map[string]string{"action": "block"}), 403)
	mustStatus(t, apiCall(h, "POST", "/api/members/"+owner.ID+"/moderate", ownerToken, map[string]string{"action": "block"}), 403)
	mustStatus(t, apiCall(h, "POST", "/api/members/"+guest.ID+"/moderate", ownerToken, map[string]string{"action": "block"}), 200)
	mustStatus(t, apiCall(h, "GET", "/api/workspace", guestToken, nil), 401)
	mustStatus(t, apiCall(h, "POST", "/api/auth", "", map[string]string{"name": "Guest", "password": testConfig().Password, "profileKey": guestKey}), 403)
	mustStatus(t, apiCall(h, "POST", "/api/join", guestToken, map[string]string{"name": "Guest", "password": testConfig().Password, "room": "lounge"}), 401)
	reloaded, err := openWorkspace(path, key)
	if err != nil {
		t.Fatal(err)
	}
	h = newWorkspaceHandler(testConfig(), reloaded, nil)
	mustStatus(t, apiCall(h, "POST", "/api/members/"+guest.ID+"/moderate", ownerToken, map[string]string{"action": "unblock"}), 200)
	w = apiCall(h, "GET", "/api/profile", guestToken, nil)
	mustStatus(t, w, 200)
	var m member
	json.Unmarshal(w.Body.Bytes(), &m)
	if m.Name != "Renamed" || m.Avatar != avatar || m.KeyHash != "" {
		t.Fatal("profile persistence mismatch")
	}
}
func TestRejectUnsafeAvatar(t *testing.T) {
	for _, value := range []string{"data:image/svg+xml;base64,PHN2Zz4=", "data:image/png;base64,dGVzdA=="} {
		if validAvatar(value) {
			t.Fatal("invalid avatar accepted")
		}
	}
}
