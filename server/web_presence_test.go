package main

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"slices"
	"testing"
	"time"
)

func TestWebPresenceIndependentOfVoiceAndMultipleTabs(t *testing.T) {
	s, _ := openWorkspace("", "")
	h := newWorkspaceHandler(testConfig(), s, nil)
	viewer, viewerToken, _ := appLogin(t, h, "Viewer", "")
	member, token, _ := appLogin(t, h, "Member", "")
	mustStatus(t, apiCall(h, "GET", "/api/web-presence", "", nil), 401)
	snapshot := func() []string {
		t.Helper()
		w := apiCall(h, "GET", "/api/workspace", viewerToken, nil)
		mustStatus(t, w, 200)
		var state struct{ OnlineMembers []string }
		if err := json.Unmarshal(w.Body.Bytes(), &state); err != nil {
			t.Fatal(err)
		}
		return state.OnlineMembers
	}
	open := func(token string, expected int) func() {
		t.Helper()
		ctx, cancel := context.WithCancel(context.Background())
		r := httptest.NewRequestWithContext(ctx, http.MethodGet, "/api/web-presence", nil)
		r.Header.Set("Authorization", "Bearer "+token)
		done := make(chan struct{})
		go func() {
			defer close(done)
			h.ServeHTTP(httptest.NewRecorder(), r)
		}()
		t.Cleanup(func() { cancel(); <-done })
		deadline := time.Now().Add(time.Second)
		for {
			s.mu.Lock()
			count := s.webTabs.total
			s.mu.Unlock()
			if count == expected {
				break
			}
			if time.Now().After(deadline) {
				t.Fatal("watch did not open")
			}
			time.Sleep(time.Millisecond)
		}
		return func() { cancel(); <-done }
	}
	if len(snapshot()) != 0 {
		t.Fatal("API reads alone made a member online")
	}
	first := open(token, 1)
	second := open(token, 2)
	if !slices.Equal(snapshot(), []string{member.ID}) {
		t.Fatal("two tabs should be one online member")
	}
	first()
	if !slices.Equal(snapshot(), []string{member.ID}) {
		t.Fatal("closing one tab removed the other")
	}
	second()
	if len(snapshot()) != 0 {
		t.Fatal("closed tabs remained online")
	}
	last := open(token, 1)
	mustStatus(t, apiCall(h, "DELETE", "/api/access-session", token, nil), 200)
	if len(snapshot()) != 0 {
		t.Fatal("revoked session still online")
	}
	last()
	open(viewerToken, 1)
	if !slices.Equal(snapshot(), []string{viewer.ID}) {
		t.Fatal("observer should only see self online")
	}
}

func TestFirstWebSnapshotConfirmsOnlyItsAuthenticatedMember(t *testing.T) {
	s, err := openWorkspace("", "")
	if err != nil {
		t.Fatal(err)
	}
	h := newWorkspaceHandler(testConfig(), s, nil)
	m, token, _ := appLogin(t, h, "Reloading", "")
	_, observer, _ := appLogin(t, h, "Observer", "")
	const open = "/api/workspace?webTab=open"
	mustStatus(t, apiCall(h, "GET", open, "", nil), 401)
	online := func(path, session string) []string {
		t.Helper()
		reply := apiCall(h, "GET", path, session, nil)
		mustStatus(t, reply, 200)
		var state struct{ OnlineMembers []string }
		if err := json.Unmarshal(reply.Body.Bytes(), &state); err != nil {
			t.Fatal(err)
		}
		return state.OnlineMembers
	}
	if len(online("/api/workspace", observer)) != 0 {
		t.Fatal("reading the roster registered a tab")
	}
	if !slices.Equal(online(open, token), []string{m.ID}) ||
		!slices.Equal(online("/api/workspace", observer), []string{m.ID}) {
		t.Fatal("opening tab and observer should agree on the first roster")
	}
	// If the browser disappears between the first snapshot and the long poll,
	// no request is held and its entry expires within the normal renewal grace.
	s.mu.Lock()
	held := s.webTabs.total
	future := s.onlineMembersLocked(time.Now().Add(webTabRenewalGrace))
	s.mu.Unlock()
	if held != 0 || len(future) != 0 {
		t.Fatal("first snapshot left permanent presence")
	}
	mustStatus(t, apiCall(h, "DELETE", "/api/access-session", token, nil), 200)
	if len(online("/api/workspace", observer)) != 0 {
		t.Fatal("revoked session still online after first snapshot")
	}
}

func TestWebPresenceRenewalBound(t *testing.T) {
	s, _ := openWorkspace("", "")
	s.data.Members = []member{{ID: "member"}}
	s.data.Sessions = map[string]rememberedSession{"session": {MemberID: "member", Expires: time.Now().Add(time.Hour).Unix()}}
	if !s.openWebTab("session") || !s.openWebTab("session") {
		t.Fatal("tabs refused")
	}
	// A normal renewal overlaps the last other tab closing. A blackholed
	// browser cannot keep the member online after the grace deadline.
	s.closeWebTab("session", true)
	s.closeWebTab("session", false)
	s.mu.Lock()
	defer s.mu.Unlock()
	defer s.webTabs.sessions["session"].grace.Stop()
	now := time.Now()
	if len(s.onlineMembersLocked(now)) != 1 || len(s.onlineMembersLocked(now.Add(webTabRenewalGrace))) != 0 {
		t.Fatal("incorrect renewal lifetime")
	}
}
