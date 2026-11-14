package main

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
	"unicode"
	"unicode/utf16"
	"unicode/utf8"
)

type channel struct {
	Posting     *channelPostingPolicy `json:"posting,omitempty"`
	Icon        string                `json:"icon,omitempty"`
	ID          string                `json:"id"`
	Name        string                `json:"name"`
	Kind        string                `json:"kind"`
	Description string                `json:"description"`
	Archived    bool                  `json:"archived,omitempty"`
}
type member struct {
	RoleIDs        []string `json:"roleIds"`
	Permissions    []string `json:"permissions,omitempty"`
	ChatMutedUntil int64    `json:"chatMutedUntil,omitempty"`
	ID             string   `json:"id"`
	Name           string   `json:"name"`
	Role           string   `json:"role"`
	KeyHash        string   `json:"keyHash,omitempty"`
	Avatar         string   `json:"avatar,omitempty"`
	AvatarVersion  string   `json:"avatarVersion,omitempty"`
	BlockedUntil   int64    `json:"blockedUntil,omitempty"`
	Blocked        bool     `json:"blocked,omitempty"`
}
type chatMessage struct {
	Format        string              `json:"format,omitempty"`
	EditedAt      int64               `json:"editedAt,omitempty"`
	Attachments   []fileAttachment    `json:"attachments,omitempty"`
	Attachment    *fileAttachment     `json:"attachment,omitempty"`
	Mentions      []chatMention       `json:"mentions,omitempty"`
	MentionUnread []string            `json:"mentionUnread,omitempty"`
	ReplyTo       string              `json:"replyTo,omitempty"`
	Reply         *messageQuote       `json:"reply,omitempty"`
	Deleted       bool                `json:"deleted,omitempty"`
	Reactions     map[string][]string `json:"reactions,omitempty"`
	ID            string              `json:"id"`
	AuthorID      string              `json:"authorId"`
	Name          string              `json:"name"`
	Text          string              `json:"text"`
	CreatedAt     int64               `json:"createdAt"`
	ClientID      string              `json:"clientId"`
}
type workspaceData struct {
	AntivirusScan    *bool                        `json:"antivirusScan,omitempty"`
	Files            map[string]filePolicy        `json:"files,omitempty"`
	OwnerBootstrap   *ownerBootstrap              `json:"ownerBootstrap,omitempty"`
	InvitationsReady bool                         `json:"invitationsReady,omitempty"`
	Invitations      []invitation                 `json:"invitations,omitempty"`
	Storage          storagePolicy                `json:"storage,omitempty"`
	Sections         []channelSection             `json:"sections,omitempty"`
	ChannelSections  map[string]string            `json:"channelSections,omitempty"`
	OwnerRoleName    string                       `json:"ownerRoleName,omitempty"`
	Roles            []workspaceRole              `json:"roles,omitempty"`
	Avatar           string                       `json:"avatar,omitempty"`
	SectionOrder     []string                     `json:"sectionOrder,omitempty"`
	Sessions         map[string]rememberedSession `json:"sessions,omitempty"`
	Activity         map[string]activity          `json:"activity,omitempty"`
	Version          int                          `json:"version"`
	Name             string                       `json:"name"`
	Description      string                       `json:"description"`
	Channels         []channel                    `json:"channels"`
	Members          []member                     `json:"members"`
	Messages         map[string][]chatMessage     `json:"messages"`
}

// One API process owns this store. A committed write replaces the complete file;
// failed writes leave both the previous file and the in-memory state intact.
type workspaceStore struct {
	mediaRevision string
	mediaChanged  chan struct{}
	mu            sync.Mutex
	path          string
	ownerKey      string
	data          workspaceData
}
type apiError struct {
	code    int
	message string
}

func (e *apiError) Error() string         { return e.message }
func fail(code int, message string) error { return &apiError{code, message} }
func report(w http.ResponseWriter, err error) {
	var e *apiError
	if errors.As(err, &e) {
		reject(w, e.code, e.message)
	} else {
		reject(w, 500, "Не удалось сохранить изменения. Повторите попытку.")
	}
}
func randomKey() string {
	var b [32]byte
	if _, err := rand.Read(b[:]); err != nil {
		panic(err)
	}
	return base64.RawURLEncoding.EncodeToString(b[:])
}
func keyHash(key string) string {
	sum := sha256.Sum256([]byte(key))
	return base64.RawURLEncoding.EncodeToString(sum[:])
}
func validKey(key string) bool {
	b, err := base64.RawURLEncoding.DecodeString(key)
	return err == nil && len(b) == 32
}
func validLabel(value string, max int) bool {
	return utf8.ValidString(value) && utf8.RuneCountInString(value) >= 1 && utf8.RuneCountInString(value) <= max && strings.IndexFunc(value, unicode.IsControl) < 0
}
func openWorkspace(path, ownerKey string) (*workspaceStore, error) {
	s := &workspaceStore{path: path, ownerKey: ownerKey, data: workspaceData{Version: 1, Name: "Свои люди",
		Channels: []channel{
			{ID: "lounge", Name: "Гостиная", Kind: "voice", Description: "Без повестки и лишних слов."},
			{ID: "test", Name: "Проверка звука", Kind: "voice", Description: "Место, чтобы проверить связь."},
			{ID: "general", Name: "общий", Kind: "text", Description: "Сообщения для своих."},
		},
		Members: []member{}, Messages: map[string][]chatMessage{}}}
	if path == "" {
		return s, nil
	}
	b, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		err = s.persist(s.data)
		return s, err
	}
	if err != nil {
		return nil, err
	}
	if err = json.Unmarshal(b, &s.data); err != nil {
		return nil, err
	}
	if s.data.Version != 1 || s.data.Messages == nil || s.data.Channels == nil {
		return nil, errors.New("unsupported workspace store")
	}
	return s, nil
}
func (s *workspaceStore) persist(data workspaceData) error {
	if s.path == "" {
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(s.path), 0700); err != nil {
		return err
	}
	f, err := os.CreateTemp(filepath.Dir(s.path), ".nimbo-*.tmp")
	if err != nil {
		return err
	}
	defer os.Remove(f.Name())
	if err = json.NewEncoder(f).Encode(data); err == nil {
		err = f.Sync()
	}
	closeErr := f.Close()
	if err != nil {
		return err
	}
	if closeErr != nil {
		return closeErr
	}
	return os.Rename(f.Name(), s.path)
}
func (s *workspaceStore) update(fn func(*workspaceData) error) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.updateLocked(fn)
}

func (s *workspaceStore) updateLocked(fn func(*workspaceData) error) error {
	b, _ := json.Marshal(s.data)
	var next workspaceData
	if err := json.Unmarshal(b, &next); err != nil {
		return err
	}
	if err := fn(&next); err != nil {
		return err
	}
	if err := s.enforceStorage(&next); err != nil {
		return err
	}
	if err := s.persist(next); err != nil {
		return err
	}
	if removedChatContent(&s.data, &next) {
		s.mediaRevision = randomKey()
		if s.mediaChanged != nil {
			close(s.mediaChanged)
			s.mediaChanged = nil
		}
	}
	s.removeUnusedFiles(&s.data, &next)
	s.data = next
	return nil
}
func findMember(data *workspaceData, id string) *member {
	for i := range data.Members {
		if data.Members[i].ID == id {
			return &data.Members[i]
		}
	}
	return nil
}
func findChannel(data *workspaceData, id string) *channel {
	for i := range data.Channels {
		if data.Channels[i].ID == id && !data.Channels[i].Archived {
			return &data.Channels[i]
		}
	}
	return nil
}
func (s *workspaceStore) voiceAllowed(id string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	ch := findChannel(&s.data, id)
	return ch != nil && ch.Kind == "voice"
}
func (s *workspaceStore) login(name, key string, codes ...string) (member, string, error) {
	return s.loginWithRecovery(name, key, false, codes...)
}

func (s *workspaceStore) loginWithRecovery(name, key string, recoverExisting bool, codes ...string) (member, string, error) {
	if key == "" {
		key = randomKey()
	}
	if !validKey(key) {
		return member{}, "", fail(400, "Некорректный ключ профиля")
	}
	hash := keyHash(key)
	s.mu.Lock()
	defer s.mu.Unlock()
	if len(codes) > 0 {
		bootstrap := s.data.OwnerBootstrap
		ownerCode := bootstrap != nil && codes[0] != "" && hmac.Equal([]byte(bootstrap.Hash), []byte(keyHash(codes[0])))
		if !ownerCode {
			if recoverExisting {
				for _, m := range s.data.Members {
					if hmac.Equal([]byte(m.KeyHash), []byte(hash)) {
						if isBlocked(&m) {
							return member{}, "", fail(403, "Доступ этого профиля заблокирован владельцем.")
						}
						m.KeyHash = ""
						return m, key, nil
					}
				}
			}
			if invitationForCode(&s.data, codes[0]) < 0 {
				return member{}, "", fail(401, "Приглашение недействительно или срок его действия истёк")
			}
		}
	}
	var result member
	err := s.updateLocked(func(data *workspaceData) error {
		if len(codes) > 0 {
			claimed, matched, err := claimBootstrap(data, name, key, codes[0])
			if matched {
				result = claimed
				return err
			}
		}
		invitationIndex := -1
		if len(codes) > 0 {
			invitationIndex = invitationForCode(data, codes[0])
			if invitationIndex < 0 {
				return fail(401, "Приглашение недействительно или срок его действия истёк")
			}
		}
		for i := range data.Members {
			if hmac.Equal([]byte(data.Members[i].KeyHash), []byte(hash)) {
				if isBlocked(&data.Members[i]) {
					return fail(403, "Доступ этого профиля заблокирован владельцем.")
				}
				data.Members[i].Name = name
				result = data.Members[i]
				return nil
			}
		}
		if len(data.Members) >= 1000 {
			return fail(409, "Достигнут лимит участников теста")
		}
		result = member{ID: randomKey(), Name: name, Role: "member", KeyHash: hash}
		data.Members = append(data.Members, result)
		if invitationIndex >= 0 && data.Invitations[invitationIndex].SingleUse {
			data.Invitations = append(data.Invitations[:invitationIndex], data.Invitations[invitationIndex+1:]...)
		}
		return nil
	})
	result.KeyHash = ""
	return result, key, err
}

// App credentials have a separate signing domain from LiveKit JWTs. Roles are
// never trusted from the credential: each mutation checks the current store.
func appToken(c config, sessionKey string, expires int64) string {
	payload, _ := json.Marshal(struct {
		ID  string
		Exp int64
	}{sessionKey, expires})
	p := base64.RawURLEncoding.EncodeToString(payload)
	mac := hmac.New(sha256.New, []byte("nimbo-app-session-v2:"+c.APISecret))
	mac.Write([]byte(p))
	return p + "." + base64.RawURLEncoding.EncodeToString(mac.Sum(nil))
}
func signedSessionKey(c config, r *http.Request) string {
	header := r.Header.Get("Authorization")
	if !strings.HasPrefix(header, "Bearer ") || len(header) > 1024 {
		return ""
	}
	parts := strings.Split(strings.TrimPrefix(header, "Bearer "), ".")
	if len(parts) != 2 {
		return ""
	}
	sig, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return ""
	}
	mac := hmac.New(sha256.New, []byte("nimbo-app-session-v2:"+c.APISecret))
	mac.Write([]byte(parts[0]))
	if !hmac.Equal(sig, mac.Sum(nil)) {
		return ""
	}
	b, err := base64.RawURLEncoding.DecodeString(parts[0])
	if err != nil {
		return ""
	}
	var claims struct {
		ID  string
		Exp int64
	}
	if json.Unmarshal(b, &claims) != nil || claims.Exp <= time.Now().Unix() {
		return ""
	}
	return claims.ID
}
func decodeRequest(w http.ResponseWriter, r *http.Request, target any) bool {
	if strings.Split(r.Header.Get("Content-Type"), ";")[0] != "application/json" {
		reject(w, 415, "Нужен JSON-запрос")
		return false
	}
	limit := int64(16384)
	if r.URL.Path == "/api/profile" || r.URL.Path == "/api/workspace" || r.URL.Path == "/api/messages" {
		limit = 65536
	}
	if r.Method == "PATCH" && strings.HasPrefix(r.URL.Path, "/api/messages/") {
		limit = 131072
	}
	r.Body = http.MaxBytesReader(w, r.Body, limit)
	d := json.NewDecoder(r.Body)
	d.DisallowUnknownFields()
	if d.Decode(target) != nil || d.Decode(new(any)) != io.EOF {
		reject(w, 400, "Некорректный запрос")
		return false
	}
	return true
}
func requireManager(data *workspaceData, id string, owner bool) error {
	if owner {
		m := findMember(data, id)
		if m == nil || m.Role != "owner" {
			return fail(403, "Недостаточно прав")
		}
		return nil
	}
	return requirePermission(data, id, "manage_channels")
}
func (s *workspaceStore) register(mux *http.ServeMux, c config, presence *presenceService) {
	writeRate := &limiter{clients: map[string]attempt{}}
	handle := func(pattern string, fn func(http.ResponseWriter, *http.Request, string)) {
		mux.HandleFunc(pattern, func(w http.ResponseWriter, r *http.Request) {
			if !allowedAPIOrigin(c, r) {
				reject(w, 403, "Недопустимый источник запроса")
				return
			}
			id := s.sessionMember(c, r)
			s.mu.Lock()
			m := findMember(&s.data, id)
			exists := m != nil && !isBlocked(m)
			s.mu.Unlock()
			if !exists {
				reject(w, 401, "Войдите в Nimbo заново")
				return
			}
			if r.Method != "GET" && !writeRate.allowLimit(id, time.Now(), 120) {
				reject(w, 429, "Слишком часто. Подождите минуту.")
				return
			}
			fn(w, r, id)
		})
	}
	handle("GET /api/workspace", func(w http.ResponseWriter, r *http.Request, id string) {
		s.mu.Lock()
		channels := []channel{}
		members := []member{}
		ownerExists := false
		for _, ch := range s.data.Channels {
			if !ch.Archived {
				channels = append(channels, ch)
			}
		}
		for _, m := range s.data.Members {
			m.KeyHash = ""
			m.Blocked = isBlocked(&m)
			if !m.Blocked {
				m.BlockedUntil = 0
			}
			m.Avatar = ""
			m.RoleIDs = memberRoles(&m)
			m.Permissions = permissionsFor(&s.data, &m)
			members = append(members, m)
			ownerExists = ownerExists || m.Role == "owner"
		}
		invitations := []invitation{}
		if hasPermission(&s.data, id, "manage_server") {
			invitations = activeInvitations(&s.data)
		}
		definitions := rolesFor(&s.data)
		sections := sectionOrder(&s.data)
		sectionDefinitions := append([]channelSection{}, sectionsFor(&s.data)...)
		assignments := s.data.ChannelSections
		name := s.data.Name
		description := s.data.Description
		avatar := s.data.Avatar
		notifications := mentionNotices(&s.data, id)
		chatActivity := activityFor(&s.data, id)
		s.mu.Unlock()
		live, fresh := presence.snapshot()
		jsonReply(w, 200, map[string]any{
			"invitations":       invitations,
			"chatActivity":      chatActivity,
			"notifications":     notifications,
			"roles":             definitions,
			"sectionOrder":      sections,
			"sections":          sectionDefinitions,
			"channelSections":   assignments,
			"name":              name,
			"description":       description,
			"avatar":            avatar,
			"channels":          channels,
			"members":           members,
			"selfId":            id,
			"ownerExists":       ownerExists,
			"presence":          live,
			"presenceAvailable": fresh,
			"serverTime":        time.Now().UnixMilli(),
		})
	})
	s.registerSections(handle)
	s.registerStorage(handle)
	s.registerFiles(handle)
	s.registerServerUpdates(handle)
	s.registerLinkPreviews(handle)
	s.registerFileMedia(mux, c)
	s.registerRoles(handle)
	s.registerInvitations(handle)
	s.registerProfile(handle)
	s.registerModeration(handle, presence)
	mux.HandleFunc("POST /api/owner", func(w http.ResponseWriter, r *http.Request) {
		reject(w, 403, "Владелец назначается только через консоль сервера")
	})
	handle("PATCH /api/members/{id}", func(w http.ResponseWriter, r *http.Request, id string) {
		var req struct {
			Role string `json:"role"`
		}
		if !decodeRequest(w, r, &req) {
			return
		}
		err := s.update(func(data *workspaceData) error {
			if err := requirePermission(data, id, "manage_roles"); err != nil {
				return err
			}
			m := findMember(data, r.PathValue("id"))
			if m == nil {
				return fail(404, "Участник не найден")
			}
			if findMember(data, id).Role != "owner" && ((m.Role != "member" || hasPermission(data, m.ID, "manage_roles")) || req.Role != "admin" || isBlocked(m)) {
				return fail(403, "Администратор может только назначить обычного участника администратором")
			}
			if m.Role == "owner" || (req.Role != "admin" && req.Role != "member") {
				return fail(400, "Можно назначить или снять только роль администратора")
			}
			if req.Role == "admin" && !canGrant(data, id, roleFor(data, "admin").Permissions) {
				return fail(403, "Недостаточно прав")
			}
			m.Role = req.Role
			m.RoleIDs = []string{}
			if req.Role == "admin" {
				m.RoleIDs = []string{"admin"}
			}
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, map[string]bool{"ok": true})
	})
	handle("PATCH /api/workspace", func(w http.ResponseWriter, r *http.Request, id string) {
		var req struct {
			Name        string  `json:"name"`
			Description *string `json:"description"`
			Avatar      *string `json:"avatar"`
		}
		if !decodeRequest(w, r, &req) {
			return
		}
		req.Name = strings.TrimSpace(req.Name)
		err := s.update(func(data *workspaceData) error {
			if err := requirePermission(data, id, "manage_server"); err != nil {
				return err
			}
			if !validLabel(req.Name, 60) {
				return fail(400, "Название: от 1 до 60 символов")
			}
			if req.Avatar != nil {
				if !validAvatar(*req.Avatar) {
					return fail(400, "Некорректный аватар сервера")
				}
				data.Avatar = *req.Avatar
			}
			data.Name = req.Name
			if req.Description != nil {
				value := strings.TrimSpace(*req.Description)
				if value != "" && !validLabel(value, 200) {
					return fail(400, "Подпись: до 200 символов")
				}
				data.Description = value
			}
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, map[string]bool{"ok": true})
	})
	mutateChannel := func(w http.ResponseWriter, r *http.Request, id string) {
		var req struct {
			Posting     *channelPostingPolicy `json:"posting"`
			Section     *string               `json:"section"`
			Icon        *string               `json:"icon"`
			Name        string                `json:"name"`
			Kind        string                `json:"kind"`
			Description string                `json:"description"`
		}
		if r.Method != "DELETE" && !decodeRequest(w, r, &req) {
			return
		}
		req.Name = strings.TrimSpace(req.Name)
		req.Description = strings.TrimSpace(req.Description)
		if req.Icon != nil && !validChannelIcon(*req.Icon) {
			reject(w, 400, "Неизвестная иконка")
			return
		}
		var result channel
		err := s.update(func(data *workspaceData) error {
			if err := requireManager(data, id, false); err != nil {
				return err
			}
			if r.Method != "DELETE" && (!validLabel(req.Name, 60) || (req.Description != "" && !validLabel(req.Description, 200))) {
				return fail(400, "Название: 1–60 символов, описание: до 200")
			}
			if r.Method == "POST" {
				if req.Kind != "voice" && req.Kind != "text" {
					return fail(400, "Выберите голосовую или текстовую комнату")
				}
				// Deleted channel metadata still occupies the bounded store; its messages are removed on DELETE.
				if len(data.Channels) >= 64 {
					return fail(409, "Достигнут лимит комнат теста (64, включая архив)")
				}
				result = channel{ID: randomKey(), Name: req.Name, Kind: req.Kind, Description: req.Description}
				if req.Icon != nil {
					result.Icon = *req.Icon
				}
				sectionID := ""
				if req.Section != nil {
					sectionID = *req.Section
				}
				if err := assignSection(data, result, sectionID); err != nil {
					return err
				}
				if err := setChannelPosting(data, &result, req.Posting); err != nil {
					return err
				}
				data.Channels = append(data.Channels, result)
				return nil
			}
			ch := findChannel(data, r.PathValue("id"))
			if ch == nil {
				return fail(404, "Комната уже удалена")
			}
			if r.Method == "DELETE" {
				ch.Archived = true
				delete(data.Messages, ch.ID)
			} else {
				if req.Section != nil {
					if err := assignSection(data, *ch, *req.Section); err != nil {
						return err
					}
				}
				if err := setChannelPosting(data, ch, req.Posting); err != nil {
					return err
				}
				ch.Name = req.Name
				ch.Description = req.Description
				if req.Icon != nil {
					ch.Icon = *req.Icon
				}
			}
			result = *ch
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, result)
	}
	handle("POST /api/channels", mutateChannel)
	handle("PATCH /api/channels/{id}", mutateChannel)
	handle("DELETE /api/channels/{id}", mutateChannel)
	if presence != nil {
		handle("POST /api/voice-state", presence.voiceState)
	}
	handle("PATCH /api/channel-order", s.reorderChannels)
	handle("GET /api/messages", s.chatHistory)
	handle("POST /api/mentions/read", s.readMentions)
	handle("PUT /api/messages/{id}/reactions", s.react)
	handle("DELETE /api/messages/{id}", s.deleteMessage)
	handle("PATCH /api/messages/{id}", s.editMessage)
	handle("POST /api/messages", func(w http.ResponseWriter, r *http.Request, id string) {
		var req struct {
			Channel  string        `json:"channel"`
			Text     string        `json:"text"`
			ReplyTo  string        `json:"replyTo"`
			ClientID string        `json:"clientId"`
			Mentions []chatMention `json:"mentions"`
			Format   string        `json:"format,omitempty"`
		}
		if !decodeRequest(w, r, &req) {
			return
		}
		leading := len(utf16.Encode([]rune(req.Text))) - len(utf16.Encode([]rune(strings.TrimLeftFunc(req.Text, unicode.IsSpace))))
		for i := range req.Mentions {
			req.Mentions[i].Start -= leading
			req.Mentions[i].End -= leading
		}
		format, formatErr := messageFormat(req.Text, req.Format, leading)
		if formatErr != nil {
			report(w, formatErr)
			return
		}
		req.Text = strings.TrimSpace(req.Text)
		format = format[:min(len(format), len(utf16.Encode([]rune(req.Text))))]
		if !utf8.ValidString(req.Text) ||
			len(req.Text) > 32000 ||
			utf8.RuneCountInString(req.Text) < 1 ||
			utf8.RuneCountInString(req.Text) > 8000 ||
			!validKey(req.ClientID) ||
			strings.IndexFunc(req.Text, func(r rune) bool { return unicode.IsControl(r) && r != '\n' && r != '\t' }) >= 0 {
			reject(w, 400, "Сообщение: от 1 до 8000 символов")
			return
		}
		var result chatMessage
		err := s.update(func(data *workspaceData) error {
			ch := findChannel(data, req.Channel)
			if ch == nil || ch.Kind != "text" {
				return fail(404, "Текстовая комната недоступна")
			}
			if findMember(data, id).ChatMutedUntil > time.Now().UnixMilli() {
				return fail(403, "Возможность писать временно ограничена модератором")
			}
			if err := requireChannelWriter(data, ch, id); err != nil {
				return err
			}
			messages := data.Messages[ch.ID]
			for _, m := range messages {
				if m.AuthorID == id && m.ClientID == req.ClientID {
					result = withReply(m, messages)
					return nil
				}
			}
			if req.ReplyTo != "" {
				found := false
				for _, m := range messages {
					if m.ID == req.ReplyTo && !m.Deleted {
						found = true
						break
					}
				}
				if !found {
					return fail(400, "Сообщение для ответа недоступно")
				}
			}
			mentions, recipients, err := resolveMentions(data, id, req.Text, req.Mentions)
			if err != nil {
				return err
			}
			recipients = replyRecipients(data, messages, id, req.ReplyTo, recipients)
			result = chatMessage{Mentions: mentions, MentionUnread: recipients, ReplyTo: req.ReplyTo, ID: randomKey(), AuthorID: id, Name: findMember(data, id).Name, Text: req.Text, CreatedAt: time.Now().UnixMilli(), ClientID: req.ClientID}
			result.Format = format
			messages = append(messages, result)
			data.Messages[ch.ID] = messages
			result = withReply(result, messages)
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, result)
	})
}
