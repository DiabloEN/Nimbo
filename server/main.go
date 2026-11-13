package main

import (
	"context"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"embed"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"time"
	"unicode"
	"unicode/utf8"
)

//go:embed all:ui
var assets embed.FS

type config struct {
	Listen, Origin, MediaURL, APIKey, APISecret, Password string
}

func loadConfig() (config, error) {
	c := config{os.Getenv("NIMBO_LISTEN"), os.Getenv("NIMBO_ORIGIN"), os.Getenv("LIVEKIT_URL"),
		os.Getenv("LIVEKIT_API_KEY"), os.Getenv("LIVEKIT_API_SECRET"), os.Getenv("NIMBO_TEST_PASSWORD")}
	if c.Listen == "" {
		c.Listen = "127.0.0.1:8090"
	}
	if len(c.Password) < 16 || len(c.APISecret) < 32 || c.APIKey == "" {
		return c, errors.New("set a test password (16+ characters), LiveKit key and secret (32+ characters)")
	}
	for _, item := range []struct{ value, secure, local string }{{c.Origin, "https", "http"}, {c.MediaURL, "wss", "ws"}} {
		u, err := url.Parse(item.value)
		if err != nil || u.Host == "" || u.User != nil || u.RawQuery != "" || u.Fragment != "" || (u.Path != "" && u.Path != "/") {
			return c, errors.New("invalid origin or media URL")
		}
		local := u.Hostname() == "localhost" || u.Hostname() == "127.0.0.1" || u.Hostname() == "::1"
		if u.Scheme != item.secure && !(local && u.Scheme == item.local) {
			return c, errors.New("public endpoints require HTTPS/WSS; plain HTTP is only for loopback development")
		}
	}
	c.Origin = strings.TrimRight(c.Origin, "/")
	return c, nil
}

type attempt struct {
	start time.Time
	count int
}
type limiter struct {
	mu      sync.Mutex
	clients map[string]attempt
}

func (l *limiter) allow(ip string, now time.Time) bool { return l.allowLimit(ip, now, 15) }
func (l *limiter) allowLimit(ip string, now time.Time, limit int) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	for key, entry := range l.clients {
		if now.Sub(entry.start) >= time.Minute {
			delete(l.clients, key)
		}
	}
	entry, exists := l.clients[ip]
	if !exists {
		if len(l.clients) >= 4096 {
			return false
		}
		entry.start = now
	}
	entry.count++
	l.clients[ip] = entry
	return entry.count <= limit
}

func jsonReply(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}

func reject(w http.ResponseWriter, status int, message string) {
	jsonReply(w, status, map[string]string{"error": message})
}

func makeToken(c config, identity, name, room string, now time.Time) (string, error) {
	claims := map[string]any{"iss": c.APIKey, "sub": identity, "name": name, "iat": now.Unix(), "nbf": now.Add(-30 * time.Second).Unix(), "exp": now.Add(10 * time.Minute).Unix(),
		"video": map[string]any{"roomJoin": true, "room": room, "canPublish": true, "canSubscribe": true, "canPublishData": false}}
	payload, err := json.Marshal(claims)
	if err != nil {
		return "", err
	}
	encode := base64.RawURLEncoding.EncodeToString
	unsigned := encode([]byte(`{"alg":"HS256","typ":"JWT"}`)) + "." + encode(payload)
	mac := hmac.New(sha256.New, []byte(c.APISecret))
	_, _ = mac.Write([]byte(unsigned))
	return unsigned + "." + encode(mac.Sum(nil)), nil
}

func newHandler(c config) http.Handler {
	store, _ := openWorkspace("", "")
	return newWorkspaceHandler(c, store, nil)
}

func newWorkspaceHandler(c config, store *workspaceStore, presence *presenceService) http.Handler {
	if err := store.ensureInvitations(c.Password); err != nil {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			reject(w, 503, "Не удалось подготовить приглашения")
		})
	}
	mux := http.NewServeMux()
	store.register(mux, c, presence)
	store.registerSessions(mux, c)
	adminPassword, adminKey := os.Getenv("NIMBO_ADMIN_PASSWORD"), os.Getenv("NIMBO_ADMIN_PROFILE_KEY")
	rate := &limiter{clients: map[string]attempt{}}
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) {
		jsonReply(w, 200, map[string]string{"status": "ok", "version": serverVersion})
	})
	mux.HandleFunc("GET /api/config", func(w http.ResponseWriter, r *http.Request) {
		store.mu.Lock()
		name := store.data.Name
		store.mu.Unlock()
		jsonReply(w, 200, map[string]string{"serverName": name})
	})
	accessHandler := func(w http.ResponseWriter, r *http.Request) {
		// Native clients omit Origin; portable browsers use key/token authentication.
		if !allowedAPIOrigin(c, r) {
			reject(w, 403, "Недопустимый источник запроса")
			return
		}
		if strings.Split(r.Header.Get("Content-Type"), ";")[0] != "application/json" {
			reject(w, 415, "Нужен JSON-запрос")
			return
		}
		ip, _, _ := net.SplitHostPort(r.RemoteAddr)
		// Reverse proxy must overwrite this header. Trust it only from loopback.
		if parsed := net.ParseIP(ip); parsed != nil && parsed.IsLoopback() {
			if forwarded := net.ParseIP(r.Header.Get("X-Real-IP")); forwarded != nil {
				ip = forwarded.String()
			}
		}
		if !rate.allow(ip, time.Now()) {
			w.Header().Set("Retry-After", "60")
			reject(w, 429, "Слишком много попыток. Подождите минуту.")
			return
		}
		r.Body = http.MaxBytesReader(w, r.Body, 4096)
		var request struct {
			ProfileKey string `json:"profileKey"`
			Name       string `json:"name"`
			Password   string `json:"password"`
			Room       string `json:"room"`
		}
		decoder := json.NewDecoder(r.Body)
		decoder.DisallowUnknownFields()
		if decoder.Decode(&request) != nil || decoder.Decode(new(any)) != io.EOF {
			reject(w, 400, "Некорректный запрос")
			return
		}
		if r.URL.Path == "/api/profile-session" {
			// A portable profile proves an existing identity; it cannot enroll a new
			// member, change their name or grant a role without an invitation.
			if !validKey(request.ProfileKey) {
				reject(w, 401, "В файле нет действующего доступа к этому серверу. Для первого входа нужен код приглашения.")
				return
			}
			hash := keyHash(request.ProfileKey)
			var current member
			store.mu.Lock()
			for _, m := range store.data.Members {
				if hmac.Equal([]byte(m.KeyHash), []byte(hash)) && !isBlocked(&m) {
					current = m
					current.KeyHash = ""
					break
				}
			}
			store.mu.Unlock()
			if current.ID == "" {
				reject(w, 401, "В файле нет действующего доступа к этому серверу. Для первого входа нужен код приглашения.")
				return
			}
			token, err := store.remember(w, r, c, current.ID)
			if err != nil {
				report(w, err)
				return
			}
			jsonReply(w, 200, map[string]any{"member": current, "profileKey": request.ProfileKey, "session": token})
			return
		}
		// This endpoint uses the same strict Origin check as authentication.
		// It creates membership only, without changing any remembered session.
		if r.URL.Path == "/api/enroll" {
			request.Name = strings.TrimSpace(request.Name)
			if !validKey(request.ProfileKey) || !utf8.ValidString(request.Name) || utf8.RuneCountInString(request.Name) < 1 || utf8.RuneCountInString(request.Name) > 40 || strings.IndexFunc(request.Name, unicode.IsControl) >= 0 {
				reject(w, 400, "Некорректное имя или ключ профиля")
				return
			}
			m, key, err := store.loginWithRecovery(request.Name, request.ProfileKey, true, request.Password)
			if err != nil {
				report(w, err)
				return
			}
			store.mu.Lock()
			name := store.data.Name
			store.mu.Unlock()
			jsonReply(w, 200, map[string]any{"serverName": name, "member": m, "profileKey": key})
			return
		}
		given := sha256.Sum256([]byte(request.Password))
		adminHash := sha256.Sum256([]byte(adminPassword))
		adminLogin := r.URL.Path == "/api/auth" && len(adminPassword) >= 24 && validKey(adminKey) && subtle.ConstantTimeCompare(given[:], adminHash[:]) == 1
		id := store.sessionMember(c, r)
		store.mu.Lock()
		authMember := findMember(&store.data, id)
		validSession := authMember != nil && !isBlocked(authMember)
		var authName string
		if validSession {
			authName = authMember.Name
		}
		store.mu.Unlock()
		if r.URL.Path == "/api/join" && !validSession {
			reject(w, 401, "Сначала войдите по приглашению")
			return
		}
		request.Name = strings.TrimSpace(request.Name)
		if !utf8.ValidString(request.Name) || utf8.RuneCountInString(request.Name) < 1 || utf8.RuneCountInString(request.Name) > 40 || strings.IndexFunc(request.Name, unicode.IsControl) >= 0 {
			reject(w, 400, "Имя должно содержать от 1 до 40 символов")
			return
		}
		if r.URL.Path == "/api/auth" {
			if adminLogin {
				// This code selects an existing identity; it never grants or restores a role.
				store.mu.Lock()
				found := false
				for _, m := range store.data.Members {
					if m.KeyHash == keyHash(adminKey) {
						request.Name = m.Name
						found = true
						break
					}
				}
				store.mu.Unlock()
				if !found {
					reject(w, 401, "Тестовый администратор не настроен")
					return
				}
				request.ProfileKey = adminKey
			}
			var codes []string
			if !adminLogin {
				codes = []string{request.Password}
			}
			m, key, err := store.login(request.Name, request.ProfileKey, codes...)
			if err != nil {
				report(w, err)
				return
			}
			token, err := store.remember(w, r, c, m.ID)
			if err != nil {
				report(w, err)
				return
			}
			jsonReply(w, 200, map[string]any{"name": m.Name, "member": m, "profileKey": key, "session": token})
			return
		}
		if !store.voiceAllowed(request.Room) {
			reject(w, 403, "Комната недоступна")
			return
		}
		nonce := make([]byte, 16)
		if _, err := rand.Read(nonce); err != nil {
			reject(w, 500, "Не удалось создать сессию")
			return
		}
		identity := id + "." + base64.RawURLEncoding.EncodeToString(nonce)
		request.Name = authName
		token, err := makeToken(c, identity, request.Name, request.Room, time.Now())
		if err != nil {
			reject(w, 500, "Не удалось создать сессию")
			return
		}
		jsonReply(w, 200, map[string]any{"url": c.MediaURL, "token": token, "identity": identity, "expiresIn": 600})
	}
	mux.HandleFunc("POST /api/enroll", accessHandler)
	mux.HandleFunc("POST /api/auth", accessHandler)
	mux.HandleFunc("POST /api/profile-session", accessHandler)
	mux.HandleFunc("POST /api/join", accessHandler)
	public, _ := fs.Sub(assets, "ui")
	files := http.FileServer(http.FS(public))
	mux.HandleFunc("GET /", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" && r.URL.Path != "/index.html" && r.URL.Path != "/favicon.svg" && !strings.HasPrefix(r.URL.Path, "/assets/") && !strings.HasPrefix(r.URL.Path, "/effects/") {
			http.NotFound(w, r)
			return
		}
		// Serve only existing files, never directory listings or an HTML fallback for API paths.
		file := strings.TrimPrefix(r.URL.Path, "/")
		if file == "" {
			file = "index.html"
		}
		info, err := fs.Stat(public, file)
		if err != nil || info.IsDir() {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Cache-Control", "no-cache")
		if strings.HasPrefix(r.URL.Path, "/effects/") {
			w.Header().Set("Cache-Control", "public, max-age=86400")
		}
		if strings.HasPrefix(r.URL.Path, "/assets/") {
			w.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
		}
		files.ServeHTTP(w, r)
	})
	media, _ := url.Parse(c.MediaURL)
	mediaHTTP := "https://" + media.Host
	if media.Scheme == "ws" {
		mediaHTTP = "http://" + media.Host
	}
	localConnect := ""
	if strings.HasPrefix(c.Origin, "http://") {
		localConnect = " http://localhost:* http://127.0.0.1:* http://[::1]:* ws://localhost:* ws://127.0.0.1:* ws://[::1]:*"
	}
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("Referrer-Policy", "no-referrer")
		w.Header().Set("Permissions-Policy", "camera=(self), microphone=(self), display-capture=(self), speaker-selection=(self)")
		w.Header().Set("Content-Security-Policy", fmt.Sprintf("default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self'; connect-src 'self' https: wss: %s %s%s; media-src 'self' blob:; img-src 'self' data: blob:; worker-src 'self' blob:; object-src 'none'; frame-src https://www.youtube-nocookie.com; frame-ancestors 'none'; base-uri 'none'; form-action 'self'", c.MediaURL, mediaHTTP, localConnect))
		if dir := os.Getenv("NIMBO_UPDATE_DIR"); dir != "" && strings.HasPrefix(r.URL.Path, "/api/") {
			if _, err := os.Stat(filepath.Join(dir, "maintenance")); err == nil {
				reject(w, 503, "Идёт установка обновления. Подождите несколько секунд.")
				return
			}
		}
		portableClients(c, mux).ServeHTTP(w, r)
	})
}

func main() {
	if len(os.Args) == 2 && os.Args[1] == "--version" {
		fmt.Println(serverVersion)
		return
	}
	if len(os.Args) > 1 && (os.Args[1] == "init" || os.Args[1] == "owner-code") {
		if err := runBootstrapCommand(os.Args[1], os.Args[2:]); err != nil {
			log.Fatal(err)
		}
		return
	}
	if len(os.Args) > 1 && os.Args[1] == "owner" {
		if err := runOwnerCommand(os.Args[2:]); err != nil {
			log.Fatal(err)
		}
		return
	}
	c, err := loadConfig()
	if err != nil {
		log.Fatal(err)
	}
	storePath := os.Getenv("NIMBO_DATA_FILE")
	if storePath == "" {
		storePath = "data/workspace.json"
	}
	store, err := openWorkspace(storePath, os.Getenv("NIMBO_OWNER_KEY"))
	if err != nil {
		log.Fatal("cannot open workspace store: ", err)
	}
	mediaAPI := os.Getenv("LIVEKIT_API_URL")
	if mediaAPI == "" {
		mediaAPI = "http://127.0.0.1:7880"
	}
	presence := newPresence(c, store, mediaAPI)
	metrics := newHTTPMetrics()
	srv := &http.Server{
		Addr:              c.Listen,
		Handler:           metrics.wrap(newWorkspaceHandler(c, store, presence)),
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       10 * time.Second,
		WriteTimeout:      15 * time.Second,
		IdleTimeout:       60 * time.Second,
		MaxHeaderBytes:    8192,
	}
	var metricsServer *http.Server
	if address := os.Getenv("NIMBO_METRICS_LISTEN"); address != "" {
		listener, err := metricsListener(address)
		if err != nil {
			log.Fatal(err)
		}
		metricsServer = &http.Server{Handler: metrics, ReadHeaderTimeout: 3 * time.Second, WriteTimeout: 5 * time.Second, IdleTimeout: 10 * time.Second, MaxHeaderBytes: 4096}
		go func() {
			if err := metricsServer.Serve(listener); err != nil && !errors.Is(err, http.ErrServerClosed) {
				log.Printf("metrics listener stopped: %v", err)
			}
		}()
		log.Printf("Nimbo metrics listening on %s", listener.Addr())
	}
	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()
	go presence.run(ctx)
	go func() {
		<-ctx.Done()
		shutdown, done := context.WithTimeout(context.Background(), 5*time.Second)
		defer done()
		_ = srv.Shutdown(shutdown)
		if metricsServer != nil {
			_ = metricsServer.Shutdown(shutdown)
		}
	}()
	log.Printf("Nimbo test service listening on %s", c.Listen)
	if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatal(err)
	}
}
