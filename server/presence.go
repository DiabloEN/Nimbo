package main

import (
	"bytes"
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"time"
)

// LiveKit protobuf JSON represents int64 as strings, but older versions may
// return numbers. Accept both, never approximate them through float64.
type unixSeconds int64

func (n *unixSeconds) UnmarshalJSON(b []byte) error {
	v, err := strconv.ParseInt(strings.Trim(string(b), `"`), 10, 64)
	*n = unixSeconds(v)
	return err
}

type voicePerson struct {
	Reconnecting bool        `json:"reconnecting,omitempty"`
	Identity     string      `json:"identity"`
	Name         string      `json:"name"`
	JoinedAt     unixSeconds `json:"joinedAt"`
}

func (p *voicePerson) UnmarshalJSON(b []byte) error {
	type plain voicePerson
	var data struct {
		plain
		JoinedAtSnake unixSeconds `json:"joined_at"`
	}
	if err := json.Unmarshal(b, &data); err != nil {
		return err
	}
	*p = voicePerson(data.plain)
	if p.JoinedAt == 0 {
		p.JoinedAt = data.JoinedAtSnake
	}
	return nil
}

type voicePresence struct {
	SID    string        `json:"sid"`
	Since  int64         `json:"since"`
	People []voicePerson `json:"people"`
}
type activity struct {
	SID        string `json:"sid"`
	Since      int64  `json:"since"`
	ObservedAt int64  `json:"observedAt"`
}
type presenceService struct {
	hints    map[string]voiceHint
	mu       sync.Mutex
	rooms    map[string]voicePresence
	checked  time.Time
	c        config
	url      string
	store    *workspaceStore
	client   *http.Client
	observed map[string]activity
}

func newPresence(c config, s *workspaceStore, url string) *presenceService {
	return &presenceService{rooms: map[string]voicePresence{}, c: c, store: s, url: strings.TrimRight(url, "/"), client: &http.Client{Timeout: 4 * time.Second}}
}
func (p *presenceService) snapshot() (map[string]voicePresence, bool) {
	if p == nil {
		return map[string]voicePresence{}, false
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.withHints(), time.Since(p.checked) < 12*time.Second
}
func (p *presenceService) rpc(ctx context.Context, method, room string, result any, identity ...string) error {
	payload := map[string]any{}
	if room != "" {
		payload["room"] = room
	}
	if len(identity) > 0 {
		payload["identity"] = identity[0]
	}
	return p.rpcBody(ctx, method, room, result, payload)
}
func (p *presenceService) rpcBody(ctx context.Context, method, room string, result any, payload map[string]any) error {
	grant := map[string]any{"roomList": true}
	if room != "" {
		grant = map[string]any{"roomAdmin": true, "room": room}
	}
	b, _ := json.Marshal(map[string]any{"iss": p.c.APIKey, "exp": time.Now().Add(time.Minute).Unix(), "nbf": time.Now().Add(-30 * time.Second).Unix(), "video": grant})
	enc := base64.RawURLEncoding.EncodeToString
	unsigned := enc([]byte(`{"alg":"HS256","typ":"JWT"}`)) + "." + enc(b)
	mac := hmac.New(sha256.New, []byte(p.c.APISecret))
	mac.Write([]byte(unsigned))
	token := unsigned + "." + enc(mac.Sum(nil))
	body, _ := json.Marshal(payload)
	r, err := http.NewRequestWithContext(ctx, "POST", p.url+"/twirp/livekit.RoomService/"+method, bytes.NewReader(body))
	if err != nil {
		return err
	}
	r.Header.Set("Authorization", "Bearer "+token)
	r.Header.Set("Content-Type", "application/json")
	response, err := p.client.Do(r)
	if err != nil {
		return err
	}
	defer response.Body.Close()
	if response.StatusCode != 200 {
		return errors.New("media presence unavailable")
	}
	return json.NewDecoder(io.LimitReader(response.Body, 1<<20)).Decode(result)
}
func (p *presenceService) poll(ctx context.Context) error {
	var list struct {
		Rooms []struct {
			SID  string `json:"sid"`
			Name string `json:"name"`
		} `json:"rooms"`
	}
	if err := p.rpc(ctx, "ListRooms", "", &list); err != nil {
		return err
	}
	next := map[string]voicePresence{}
	for _, room := range list.Rooms {
		if !p.store.voiceAllowed(room.Name) {
			continue
		}
		var result struct {
			Participants []voicePerson `json:"participants"`
		}
		if err := p.rpc(ctx, "ListParticipants", room.Name, &result); err != nil {
			return err
		}
		active := result.Participants[:0]
		for _, person := range result.Participants {
			memberID, _, _ := strings.Cut(person.Identity, ".")
			p.store.mu.Lock()
			m := findMember(&p.store.data, memberID)
			exists := m != nil && !isBlocked(m)
			p.store.mu.Unlock()
			if !exists {
				var removed map[string]any
				_ = p.rpc(ctx, "RemoveParticipant", room.Name, &removed, person.Identity)
				continue
			}
			active = append(active, person)
		}
		result.Participants = active
		if len(result.Participants) == 0 {
			continue
		}
		since := time.Now().UnixMilli()
		for _, person := range result.Participants {
			if int64(person.JoinedAt) > 0 && int64(person.JoinedAt)*1000 < since {
				since = int64(person.JoinedAt) * 1000
			}
		}
		next[room.Name] = voicePresence{room.SID, since, result.Participants}
	}
	// Persist the beginning of each observed non-empty interval. The browser's
	// clock and reloads cannot reset it. Polling resolution is three seconds.
	p.store.mu.Lock()
	if p.observed == nil {
		p.observed = map[string]activity{}
		for id, a := range p.store.data.Activity {
			p.observed[id] = a
		}
	}
	persisted := p.store.data.Activity
	changes := len(persisted) != len(next)
	observed := map[string]activity{}
	now := time.Now().UnixMilli()
	for id, room := range next {
		old := p.observed[id]
		if old.SID == room.SID && old.Since > 0 && room.Since <= old.ObservedAt {
			room.Since = min(old.Since, room.Since)
		}
		next[id] = room
		observed[id] = activity{room.SID, room.Since, now}
		disk := persisted[id]
		if disk.SID != room.SID || disk.Since != room.Since || now-disk.ObservedAt >= 30000 {
			changes = true
		}
	}
	p.store.mu.Unlock()
	if changes {
		if err := p.store.update(func(data *workspaceData) error { data.Activity = observed; return nil }); err != nil {
			return err
		}
	}
	p.observed = observed
	p.mu.Lock()
	p.rooms = next
	p.checked = time.Now()
	p.mu.Unlock()
	return nil
}
func (p *presenceService) run(ctx context.Context) {
	ticker := time.NewTicker(3 * time.Second)
	defer ticker.Stop()
	for {
		check, done := context.WithTimeout(ctx, 8*time.Second)
		_ = p.poll(check)
		done()
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
		}
	}
}
