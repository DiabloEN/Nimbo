package main

import (
	"encoding/base64"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestVoiceMovesAuthorizationDeliveryAndExpiry(t *testing.T) {
	s, _ := openWorkspace("", "")
	var live []voicePerson
	unavailable := false
	media := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if unavailable {
			w.WriteHeader(503)
			return
		}
		if r.URL.Path == "/twirp/livekit.RoomService/SendData" {
			var payload struct {
				Topic      string   `json:"topic"`
				Identities []string `json:"destination_identities"`
			}
			json.NewDecoder(r.Body).Decode(&payload)
			if payload.Topic != "nimbo.voice-move" || len(payload.Identities) != 1 {
				t.Error("wrong wakeup scope")
			}
			jsonReply(w, 200, map[string]any{})
			return
		}
		if r.URL.Path != "/twirp/livekit.RoomService/ListParticipants" {
			t.Errorf("unexpected media mutation: %s", r.URL.Path)
		}
		jsonReply(w, 200, map[string]any{"participants": live})
	}))
	defer media.Close()
	p := newPresence(testConfig(), s, media.URL)
	h := newWorkspaceHandler(testConfig(), s, p)
	owner, ot, _ := appLogin(t, h, "Owner", "")
	mod, mt, _ := appLogin(t, h, "Moderator", "")
	guest, gt, _ := appLogin(t, h, "Guest", "")
	s.update(func(d *workspaceData) error {
		findMember(d, owner.ID).Role = "owner"
		findMember(d, mod.ID).RoleIDs = []string{"moderator"}
		return nil
	})
	// A second voice channel is created through the same authorized API.
	created := apiCall(h, "POST", "/api/channels", ot, map[string]string{"name": "Second", "kind": "voice"})
	mustStatus(t, created, 200)
	var ch channel
	json.Unmarshal(created.Body.Bytes(), &ch)
	if ch.ID == "" {
		t.Fatal("missing destination")
	}
	identity := guest.ID + ".one"
	other := guest.ID + ".two"
	live = []voicePerson{{Identity: identity}, {Identity: other}, {Identity: owner.ID + ".one"}, {Identity: mod.ID + ".one"}}
	p.rooms["lounge"] = voicePresence{People: live}
	p.checked = time.Now()
	poll := func(token, id string) *httptest.ResponseRecorder {
		return apiCall(h, "POST", "/api/voice-control", token, map[string]string{"identity": id, "room": "lounge"})
	}
	move := func(token, id, to string) *httptest.ResponseRecorder {
		return apiCall(h, "POST", "/api/voice-move", token, map[string]string{"identity": id, "from": "lounge", "to": to})
	}
	mustStatus(t, move("", identity, ch.ID), 401)
	mustStatus(t, move(gt, mod.ID+".one", ch.ID), 403)
	mustStatus(t, move(mt, owner.ID+".one", ch.ID), 403)
	mustStatus(t, move(ot, owner.ID+".one", ch.ID), 409) // No client registration yet.
	mustStatus(t, move(ot, identity, "general"), 403)
	mustStatus(t, move(ot, identity, "lounge"), 403)
	mustStatus(t, move(ot, identity, "missing"), 403)
	mustStatus(t, move(ot, identity, ch.ID), 409) // Old client has not advertised support.
	mustStatus(t, poll(gt, owner.ID+".one"), 400)
	mustStatus(t, poll(gt, guest.ID+".invented"), 200)
	if len(p.controls) != 0 {
		t.Fatal("invented identity registered")
	}
	mustStatus(t, poll(gt, identity), 200)
	mustStatus(t, poll(gt, other), 200)
	// Disconnect permission alone no longer grants moves. Grant the independent
	// permission through the same role API used by the settings checkbox.
	mustStatus(t, move(mt, identity, ch.ID), 403)
	mustStatus(t, apiCall(h, "PATCH", "/api/roles/moderator", ot, map[string]any{"name": "Mover", "permissions": []string{"move_members"}}), 200)
	if hasPermission(&s.data, mod.ID, "kick_members") || !hasPermission(&s.data, mod.ID, "move_members") {
		t.Fatal("move and disconnect permissions are not independent")
	}
	mustStatus(t, apiCall(h, "POST", "/api/members/"+guest.ID+"/moderate", mt, map[string]string{"action": "kick"}), 403)
	// Move permission applies to everyone, including the owner and the actor.
	// Other moderation and role-edit protections remain independent.
	mustStatus(t, poll(ot, owner.ID+".one"), 200)
	mustStatus(t, move(mt, owner.ID+".one", ch.ID), 202)
	var ownerCommand struct {
		Move *voiceMove `json:"move"`
	}
	json.Unmarshal(poll(ot, owner.ID+".one").Body.Bytes(), &ownerCommand)
	if ownerCommand.Move == nil {
		t.Fatal("owner move not delivered")
	}
	mustStatus(t, apiCall(h, "POST", "/api/voice-move/join", ot, map[string]string{"identity": owner.ID + ".one", "moveId": ownerCommand.Move.ID}), 200)
	mustStatus(t, apiCall(h, "POST", "/api/members/"+owner.ID+"/moderate", mt, map[string]string{"action": "kick"}), 403)
	mustStatus(t, poll(mt, mod.ID+".one"), 200)
	mustStatus(t, move(mt, mod.ID+".one", ch.ID), 202)
	mustStatus(t, move(mt, identity, ch.ID), 202)
	mustStatus(t, move(mt, identity, ch.ID), 409)
	if p.controls[other].Move != nil {
		t.Fatal("another session was moved")
	}
	var response struct {
		Move *voiceMove `json:"move"`
	}
	json.Unmarshal(poll(gt, identity).Body.Bytes(), &response)
	if response.Move == nil || response.Move.Room != ch.ID {
		t.Fatal("command not delivered")
	}
	joinBody := map[string]string{"identity": identity, "moveId": response.Move.ID}
	p.hints = map[string]voiceHint{identity: {Room: ch.ID, Left: true, Until: time.Now().Add(30 * time.Second)}}
	admitted := apiCall(h, "POST", "/api/voice-move/join", gt, joinBody)
	mustStatus(t, admitted, 200)
	if _, stale := p.hints[identity]; stale {
		t.Fatal("return move retained an earlier departure in the destination")
	}
	var admission struct {
		Identity string
		Token    string
	}
	json.Unmarshal(admitted.Body.Bytes(), &admission)
	if admission.Identity != identity {
		t.Fatal("transfer changed connection identity")
	}
	for _, query := range []string{"", "?annotations=1", "?annotations=0"} {
		result := apiCall(h, "POST", "/api/voice-move/join"+query, gt, joinBody)
		mustStatus(t, result, 200)
		var grant struct {
			Token       string
			Annotations bool
		}
		if err := json.Unmarshal(result.Body.Bytes(), &grant); err != nil {
			t.Fatal(err)
		}
		parts := strings.Split(grant.Token, ".")
		if len(parts) != 3 {
			t.Fatal("invalid move token")
		}
		payload, err := base64.RawURLEncoding.DecodeString(parts[1])
		if err != nil {
			t.Fatal(err)
		}
		var claims struct {
			Sub   string
			Video struct {
				Room           string
				CanPublishData bool
			}
		}
		if err := json.Unmarshal(payload, &claims); err != nil {
			t.Fatal(err)
		}
		want := query == "?annotations=1"
		if grant.Annotations != want || claims.Video.CanPublishData != want || claims.Sub != identity || claims.Video.Room != ch.ID {
			t.Fatalf("move token opt-in mismatch for %q", query)
		}
	}
	mustStatus(t, apiCall(h, "POST", "/api/voice-move/join", mt, joinBody), 403)
	mustStatus(t, apiCall(h, "POST", "/api/voice-move/join", gt, map[string]string{"identity": identity, "moveId": "wrong"}), 409)
	// Poll retries retain the same command; actor IDs are never exposed.
	if p.controls[identity].Move.ID != response.Move.ID {
		t.Fatal("command changed")
	}
	s.update(func(d *workspaceData) error { findMember(d, mod.ID).RoleIDs = []string{}; return nil })
	mustStatus(t, apiCall(h, "POST", "/api/voice-move/join", gt, joinBody), 409)
	if string(poll(gt, identity).Body.Bytes()) != "{}\n" {
		t.Fatal("revoked permission delivered")
	}
	mustStatus(t, move(ot, identity, ch.ID), 202)
	p.controls[identity].Move.Until = time.Now().Add(-time.Second)
	poll(gt, identity)
	if p.controls[identity].Move != nil {
		t.Fatal("expired move retained")
	}
	live = nil
	mustStatus(t, move(ot, identity, ch.ID), 409)
	live = []voicePerson{{Identity: identity}}
	unavailable = true
	mustStatus(t, move(ot, identity, ch.ID), 502)
	unavailable = false
	p.controls[identity].Until = time.Now().Add(-time.Second)
	mustStatus(t, move(ot, identity, ch.ID), 409)
}
