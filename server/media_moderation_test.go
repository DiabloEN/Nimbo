package main

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
)

func TestMediaModerationTargetsOnlyMatchingSourceAndIdentity(t *testing.T) {
	calls := []map[string]any{}
	api := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/twirp/livekit.RoomService/ListRooms":
			jsonReply(w, 200, map[string]any{"rooms": []map[string]string{{"name": "lounge"}}})
		case "/twirp/livekit.RoomService/ListParticipants":
			tracks := []map[string]any{{"sid": "mic", "source": "MICROPHONE"}, {"sid": "camera", "source": 1}, {"sid": "screen", "source": "SCREEN_SHARE"}, {"sid": "screen-audio", "source": 4}, {"sid": "already-muted", "source": "MICROPHONE", "muted": true}}
			jsonReply(w, 200, map[string]any{
				"participants": []map[string]any{
					{"identity": "target.one", "tracks": tracks},
					{"identity": "target.two", "tracks": tracks},
					{"identity": "target-other.one", "tracks": tracks},
					{"identity": "someone.one", "tracks": tracks},
				},
			})
		case "/twirp/livekit.RoomService/MutePublishedTrack":
			var body map[string]any
			json.NewDecoder(r.Body).Decode(&body)
			calls = append(calls, body)
			jsonReply(w, 200, map[string]bool{"ok": true})
		default:
			t.Errorf("unexpected method %s", r.URL.Path)
			http.NotFound(w, r)
		}
	}))
	defer api.Close()
	p := newPresence(testConfig(), nil, api.URL)
	for action, want := range map[string][]string{"mute_mic": {"mic", "mic"}, "mute_camera": {"camera", "camera"}, "mute_screen": {"screen", "screen-audio", "screen", "screen-audio"}} {
		calls = nil
		if err := p.muteMemberMedia(context.Background(), "target", action); err != nil {
			t.Fatal(err)
		}
		if len(calls) != len(want) {
			t.Fatalf("%s calls: %v", action, calls)
		}
		for i, c := range calls {
			if c["track_sid"] != want[i] || c["muted"] != true || c["room"] != "lounge" || (c["identity"] != "target.one" && c["identity"] != "target.two") {
				t.Fatalf("wrong mute payload: %v", c)
			}
		}
	}
}

func TestMediaModerationPermissionAndServiceErrors(t *testing.T) {
	s, _ := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	h := newWorkspaceHandler(testConfig(), s, nil)
	mod, mt, _ := appLogin(t, h, "Moderator", "")
	guest, gt, _ := appLogin(t, h, "Guest", "")
	owner, ot, _ := appLogin(t, h, "Owner", "")
	s.update(func(d *workspaceData) error {
		findMember(d, mod.ID).RoleIDs = []string{"moderator"}
		findMember(d, owner.ID).Role = "owner"
		return nil
	})
	for _, action := range []string{"mute_mic", "mute_camera", "mute_screen"} {
		body := map[string]string{"action": action}
		mustStatus(t, apiCall(h, "POST", "/api/members/"+mod.ID+"/moderate", gt, body), 403)
		mustStatus(t, apiCall(h, "POST", "/api/members/"+owner.ID+"/moderate", mt, body), 403)
		mustStatus(t, apiCall(h, "POST", "/api/members/"+owner.ID+"/moderate", ot, body), 403)
		mustStatus(t, apiCall(h, "POST", "/api/members/"+guest.ID+"/moderate", mt, body), 502)
	}
	mustStatus(t, apiCall(h, "POST", "/api/members/"+guest.ID+"/moderate", mt, map[string]string{"action": "unmute_camera"}), 400)
}
