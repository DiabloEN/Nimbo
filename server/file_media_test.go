package main

import (
	"bytes"
	"encoding/json"
	"image"
	"image/png"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestMediaStreamingSessionRangeAndRevocation(t *testing.T) {
	s, err := openWorkspace(filepath.Join(t.TempDir(), "workspace.json"), "")
	if err != nil {
		t.Fatal(err)
	}
	c := testConfig()
	h := newWorkspaceHandler(c, s, nil)
	login := apiCall(h, "POST", "/api/auth", "", map[string]string{"name": "Media owner", "password": c.Password})
	mustStatus(t, login, 200)
	cookie := login.Result().Cookies()[0]
	var auth struct {
		Member  member
		Session string
	}
	_ = json.Unmarshal(login.Body.Bytes(), &auth)
	if err = s.assignOwner(auth.Member.ID, false); err != nil {
		t.Fatal(err)
	}
	mustStatus(t, apiCall(h, "PATCH", "/api/channels/general/files", auth.Session, filePolicy{Enabled: true}), 200)
	var imageData bytes.Buffer
	_ = png.Encode(&imageData, image.NewRGBA(image.Rect(0, 0, 2, 2)))
	upload := fileRequest(t, h, auth.Session, "test.png", imageData.String(), randomKey())
	mustStatus(t, upload, 200)
	var msg chatMessage
	_ = json.Unmarshal(upload.Body.Bytes(), &msg)
	path := "/api/session/media/" + msg.Attachment.ID
	get := func(method, path string, authenticated bool, origin, site, byteRange string) *httptest.ResponseRecorder {
		r := httptest.NewRequest(method, path, nil)
		if authenticated {
			r.AddCookie(cookie)
		}
		r.Header.Set("Origin", origin)
		r.Header.Set("Sec-Fetch-Site", site)
		r.Header.Set("Range", byteRange)
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		return w
	}
	mustStatus(t, get("GET", path, false, "", "", ""), 401)
	mustStatus(t, get("GET", path, true, "https://other.example", "", ""), 403)
	mustStatus(t, get("GET", path, true, "", "cross-site", ""), 403)
	w := get("GET", path, true, c.Origin, "same-origin", "bytes=0-7")
	mustStatus(t, w, 206)
	if w.Body.Len() != 8 ||
		!bytes.Equal(w.Body.Bytes(), imageData.Bytes()[:8]) ||
		w.Header().Get("Content-Type") != "image/png" ||
		w.Header().Get("X-Content-Type-Options") != "nosniff" ||
		!strings.HasPrefix(w.Header().Get("Content-Disposition"), "inline;") {
		t.Fatal("unsafe or non-streaming response")
	}
	mustStatus(t, get("HEAD", path, true, "", "same-origin", ""), 200)
	mustStatus(t, get("GET", path, true, "", "same-origin", "bytes=999999-"), 416)
	forged := fileRequest(t, h, auth.Session, "fake.png", "<script>alert(1)</script>", randomKey())
	mustStatus(t, forged, 200)
	var fake chatMessage
	_ = json.Unmarshal(forged.Body.Bytes(), &fake)
	mustStatus(t, get("GET", "/api/session/media/"+fake.Attachment.ID, true, "", "same-origin", ""), 415)
	s.mu.Lock()
	findMember(&s.data, auth.Member.ID).Blocked = true
	s.mu.Unlock()
	mustStatus(t, get("GET", path, true, "", "same-origin", "bytes=8-15"), 401)
	s.mu.Lock()
	findMember(&s.data, auth.Member.ID).Blocked = false
	entry := s.data.Sessions[keyHash(cookie.Value)]
	entry.Expires = time.Now().Add(-time.Second).Unix()
	s.data.Sessions[keyHash(cookie.Value)] = entry
	s.mu.Unlock()
	mustStatus(t, get("GET", path, true, "", "same-origin", ""), 401)
}

func TestMediaSignaturesNeverRenderActiveContent(t *testing.T) {
	for _, name := range []string{"a.html", "a.svg", "a.pdf", "a.png", "a.mp4", "a.mp3", "a.webp"} {
		if safeMediaType(name, []byte("<html><script>alert(1)</script>")) != "" {
			t.Fatal(name)
		}
	}
	b := make([]byte, 30)
	copy(b, "RIFF")
	copy(b[8:], "WEBPVP8X")
	b[24] = 255
	b[25] = 255
	b[26] = 255
	if safeMediaType("huge.webp", b) != "" {
		t.Fatal("oversize image accepted")
	}
	b[24] = 1
	b[25] = 0
	b[26] = 0
	if safeMediaType("small.webp", b) != "image/webp" {
		t.Fatal("small webp rejected")
	}
}
