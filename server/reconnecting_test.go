package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestVoiceStateAfterMembershipRevocation(t *testing.T) {
	for _, blocked := range []bool{false, true} {
		name := "removed"
		if blocked {
			name = "blocked"
		}
		t.Run(name, func(t *testing.T) {
			store := &workspaceStore{data: workspaceData{
				Channels: []channel{{ID: "voice", Kind: "voice"}},
			}}
			if blocked {
				store.data.Members = []member{{ID: "guest", Blocked: true}}
			}
			p := &presenceService{store: store}
			// Simulate revocation between middleware authentication and the handler.
			r := httptest.NewRequest(http.MethodPost, "/api/voice-state", strings.NewReader(`{"room":"voice","identity":"guest.tab","state":"left"}`))
			r.Header.Set("Content-Type", "application/json")
			w := httptest.NewRecorder()
			p.voiceState(w, r, "guest")
			if w.Code != http.StatusUnauthorized || !strings.Contains(w.Body.String(), "VOICE_MEMBER_UNAVAILABLE") {
				t.Fatalf("unexpected rejection: %d %s", w.Code, w.Body.String())
			}
			if !store.mu.TryLock() {
				t.Fatal("revoked member left the workspace locked")
			}
			store.mu.Unlock()
			if len(p.hints) != 0 {
				t.Fatal("revoked member created a presence hint")
			}
		})
	}
}
