package main

import (
	"archive/zip"
	"bufio"
	"context"
	"encoding/binary"
	"encoding/json"
	"io"
	"mime"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"
)

const maxFileBytes int64 = 20 * 1024 * 1024

var fileSlots = make(chan struct{}, 2)

type filePolicy struct {
	Enabled bool `json:"enabled"`
	Scan    bool `json:"scan"`
}
type fileAttachment struct {
	ID      string `json:"id"`
	Name    string `json:"name"`
	Size    int64  `json:"size"`
	Scanned bool   `json:"scanned"`
}

func policyForFiles(d *workspaceData, ch string) filePolicy {
	p, ok := d.Files[ch]
	if !ok {
		p = filePolicy{Scan: true}
	}
	if d.AntivirusScan != nil {
		p.Scan = *d.AntivirusScan
	}
	return p
}
func (s *workspaceStore) filePath(id string) string {
	return filepath.Join(filepath.Dir(s.path), "files", id)
}
func messageAttachments(m chatMessage) []fileAttachment {
	if len(m.Attachments) > 0 {
		return m.Attachments
	}
	if m.Attachment != nil {
		return []fileAttachment{*m.Attachment}
	}
	return nil
}
func attachmentsOf(d *workspaceData) map[string]fileAttachment {
	result := map[string]fileAttachment{}
	for _, ms := range d.Messages {
		for _, m := range ms {
			for _, a := range messageAttachments(m) {
				if validKey(a.ID) {
					result[a.ID] = a
				}
			}
		}
	}
	return result
}
func (s *workspaceStore) removeUnusedFiles(before, after *workspaceData) {
	if s.path == "" {
		return
	}
	keep := attachmentsOf(after)
	for id := range attachmentsOf(before) {
		if _, ok := keep[id]; !ok {
			_ = os.Remove(s.filePath(id))
		}
	}
}
func fileAccess(d *workspaceData, actor, ch string, write bool) error {
	m := findMember(d, actor)
	if m == nil || isBlocked(m) {
		return fail(401, "Войдите в Nimbo заново")
	}
	c := findChannel(d, ch)
	if c == nil || c.Kind != "text" {
		return fail(404, "Текстовый канал недоступен")
	}
	if write {
		if err := requireChannelWriter(d, c, actor); err != nil {
			return err
		}
		if m.ChatMutedUntil > time.Now().UnixMilli() {
			return fail(403, "Отправка сообщений временно ограничена")
		}
		if !policyForFiles(d, ch).Enabled {
			return fail(403, "Загрузка файлов отключена владельцем")
		}
	}
	return nil
}
func (s *workspaceStore) registerFiles(handle func(string, func(http.ResponseWriter, *http.Request, string))) {
	s.registerAntivirus(handle)
	handle("GET /api/channels/{id}/files", func(w http.ResponseWriter, r *http.Request, actor string) {
		s.mu.Lock()
		defer s.mu.Unlock()
		if err := fileAccess(&s.data, actor, r.PathValue("id"), false); err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, map[string]any{"policy": policyForFiles(&s.data, r.PathValue("id")), "scannerConfigured": os.Getenv("NIMBO_CLAMD_ADDRESS") != "", "maxBytes": maxFileBytes})
	})
	handle("PATCH /api/channels/{id}/files", func(w http.ResponseWriter, r *http.Request, actor string) {
		var p filePolicy
		if !decodeRequest(w, r, &p) {
			return
		}
		err := s.update(func(d *workspaceData) error {
			if err := requireManager(d, actor, true); err != nil {
				return err
			}
			if err := fileAccess(d, actor, r.PathValue("id"), false); err != nil {
				return err
			}
			if p.Enabled && s.path == "" {
				return fail(503, "Файловое хранилище не настроено")
			}
			if d.AntivirusScan != nil {
				p.Scan = *d.AntivirusScan
			}
			if p.Enabled && p.Scan && os.Getenv("NIMBO_CLAMD_ADDRESS") == "" {
				return fail(409, "Антивирус не настроен. Администратор хоста должен подключить ClamAV.")
			}
			if d.Files == nil {
				d.Files = map[string]filePolicy{}
			}
			d.Files[r.PathValue("id")] = p
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, p)
	})
	handle("POST /api/channels/{id}/files", s.uploadFile)
	handle("GET /api/files/{id}", s.downloadFile)
}
func (s *workspaceStore) uploadFile(w http.ResponseWriter, r *http.Request, actor string) {
	select {
	case fileSlots <- struct{}{}:
		defer func() { <-fileSlots }()
	default:
		reject(w, 429, "Сервер уже принимает файлы. Повторите чуть позже.")
		return
	}
	if s.path == "" {
		reject(w, 503, "Файловое хранилище не настроено")
		return
	}
	ch := r.PathValue("id")
	s.mu.Lock()
	err := fileAccess(&s.data, actor, ch, true)
	policy := policyForFiles(&s.data, ch)
	s.mu.Unlock()
	if err != nil {
		report(w, err)
		return
	}
	if policy.Scan && os.Getenv("NIMBO_CLAMD_ADDRESS") == "" {
		reject(w, 503, "Антивирус недоступен. Файл не опубликован.")
		return
	}
	// Upload/scan need longer deadlines than ordinary small API requests.
	rc := http.NewResponseController(w)
	_ = rc.SetReadDeadline(time.Now().Add(120 * time.Second))
	_ = rc.SetWriteDeadline(time.Now().Add(240 * time.Second))
	r.Body = http.MaxBytesReader(w, r.Body, maxFileBytes+65536)
	reader, e := r.MultipartReader()
	if e != nil {
		reject(w, 400, "Нужна загрузка файла")
		return
	}
	if e = os.MkdirAll(filepath.Dir(s.filePath("placeholder")), 0700); e != nil {
		report(w, e)
		return
	}
	// Temporary files are private and unpublished until every file passes validation.
	type pendingFile struct {
		path       string
		attachment fileAttachment
	}
	var pending []pendingFile
	committed := false
	defer func() {
		if !committed {
			for _, f := range pending {
				_ = os.Remove(f.path)
			}
		}
	}()
	var text, clientID, replyTo string
	var mentions []chatMention
	var size int64
	seen := map[string]bool{}
	for {
		part, e := reader.NextPart()
		if e == io.EOF {
			break
		}
		if e != nil {
			reject(w, 413, "Не удалось прочитать файл. Лимит — 20 МБ.")
			return
		}
		name := part.FormName()
		if seen[name] && name != "file" {
			reject(w, 400, "Повторное поле загрузки")
			return
		}
		seen[name] = true
		switch name {
		case "file":
			if len(pending) >= 10 {
				reject(w, 400, "Можно прикрепить до 10 файлов к сообщению")
				return
			}
			filename := filepath.Base(strings.ReplaceAll(part.FileName(), "\\", "/"))
			if !validLabel(filename, 180) || filename == "." || filename == ".." || strings.IndexFunc(filename, func(r rune) bool { return r == ':' || unicode.Is(unicode.Cf, r) }) >= 0 {
				reject(w, 400, "Некорректное имя файла")
				return
			}
			temp, err := os.CreateTemp(filepath.Dir(s.filePath("placeholder")), ".upload-*")
			if err != nil {
				report(w, err)
				return
			}
			pending = append(pending, pendingFile{path: temp.Name(), attachment: fileAttachment{ID: randomKey(), Name: filename}})
			n, copyErr := io.Copy(temp, io.LimitReader(part, maxFileBytes-size+1))
			syncErr := temp.Sync()
			closeErr := temp.Close()
			if copyErr != nil || n == 0 || n > maxFileBytes-size {
				reject(w, 413, "Общий размер непустых вложений — до 20 МБ")
				return
			}
			if syncErr != nil {
				report(w, syncErr)
				return
			}
			if closeErr != nil {
				report(w, closeErr)
				return
			}
			pending[len(pending)-1].attachment.Size = n
			size += n
		case "text", "clientId", "replyTo", "mentions":
			limit := int64(32000)
			if name != "text" && name != "mentions" {
				limit = 256
			}
			b, e := io.ReadAll(io.LimitReader(part, limit+1))
			if e != nil || int64(len(b)) > limit {
				reject(w, 400, "Слишком длинное поле")
				return
			}
			switch name {
			case "mentions":
				if json.Unmarshal(b, &mentions) != nil {
					reject(w, 400, "Некорректные упоминания")
					return
				}
			case "text":
				text = string(b)
			case "clientId":
				clientID = string(b)
			case "replyTo":
				replyTo = string(b)
			}
		default:
			reject(w, 400, "Неизвестное поле загрузки")
			return
		}
		part.Close()
	}
	if len(pending) == 0 || !validKey(clientID) || !utf8.ValidString(text) || utf8.RuneCountInString(text) > 8000 || strings.IndexFunc(text, func(r rune) bool { return unicode.IsControl(r) && r != '\n' && r != '\t' }) >= 0 {
		reject(w, 400, "Некорректное сообщение с файлом")
		return
	}
	scanned := false
	if policy.Scan {
		scanContext, cancelScan := context.WithTimeout(r.Context(), 90*time.Second)
		defer cancelScan()
		for _, f := range pending {
			if e = scanFile(scanContext, f.path); e != nil {
				report(w, e)
				return
			}
		}
		scanned = true
	}
	attachments := make([]fileAttachment, 0, len(pending))
	for i := range pending {
		f := &pending[i]
		f.attachment.Scanned = scanned
		destination := s.filePath(f.attachment.ID)
		if e = os.Rename(f.path, destination); e != nil {
			report(w, e)
			return
		}
		f.path = destination
		attachments = append(attachments, f.attachment)
	}
	var result chatMessage
	e = s.update(func(d *workspaceData) error {
		if err := fileAccess(d, actor, ch, true); err != nil {
			return err
		}
		if policyForFiles(d, ch).Scan && !scanned {
			return fail(409, "Владелец включил проверку. Загрузите файл ещё раз.")
		}
		for _, m := range d.Messages[ch] {
			if m.AuthorID == actor && m.ClientID == clientID {
				result = withReply(m, d.Messages[ch])
				return nil
			}
		}
		if replyTo != "" {
			found := false
			for _, m := range d.Messages[ch] {
				if m.ID == replyTo {
					found = true
					break
				}
			}
			if !found {
				return fail(400, "Сообщение для ответа недоступно")
			}
		}
		// Do not silently evict history to make room for an upload.
		if dataSize(d)+size+65536 > s.policyFor(d).LimitBytes*9/10 {
			return fail(507, "Недостаточно места для файла. Владелец может освободить хранилище.")
		}
		if free, _, err := diskSpace(filepath.Dir(s.path)); err == nil && free < uint64(64*mib+metadataSize(d)+65536) {
			return fail(507, "На диске мало свободного места")
		}
		result = chatMessage{ID: randomKey(), AuthorID: actor, Name: findMember(d, actor).Name, Text: text, ClientID: clientID, CreatedAt: time.Now().UnixMilli(), ReplyTo: replyTo}
		if len(attachments) == 1 {
			result.Attachment = &attachments[0]
		} else {
			result.Attachments = attachments
		}
		resolved, recipients, err := resolveMentions(d, actor, text, mentions)
		if err != nil {
			return err
		}
		result.Mentions = resolved
		result.MentionUnread = replyRecipients(d, d.Messages[ch], actor, replyTo, recipients)
		d.Messages[ch] = append(d.Messages[ch], result)
		result = withReply(result, d.Messages[ch])
		return nil
	})
	if e != nil {
		report(w, e)
		return
	}
	stored := messageAttachments(result)
	committed = len(stored) > 0 && stored[0].ID == attachments[0].ID
	jsonReply(w, 200, result)
}
func (s *workspaceStore) downloadFile(w http.ResponseWriter, r *http.Request, actor string) {
	id := r.PathValue("id")
	if !validKey(id) {
		reject(w, 404, "Файл недоступен")
		return
	}
	s.mu.Lock()
	var found *fileAttachment
	for ch, ms := range s.data.Messages {
		if fileAccess(&s.data, actor, ch, false) != nil {
			continue
		}
		for _, m := range ms {
			for _, a := range messageAttachments(m) {
				if a.ID == id {
					value := a
					found = &value
					break
				}
			}
		}
	}
	if found == nil {
		s.mu.Unlock()
		reject(w, 404, "Файл недоступен")
		return
	}
	f, err := os.Open(s.filePath(id))
	s.mu.Unlock()
	if err != nil {
		reject(w, 404, "Файл недоступен")
		return
	}
	defer f.Close()
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Disposition", mime.FormatMediaType("attachment", map[string]string{"filename": found.Name}))
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("Content-Security-Policy", "sandbox; default-src 'none'")
	w.Header().Set("Cache-Control", "private, no-store")
	_ = http.NewResponseController(w).SetWriteDeadline(time.Now().Add(120 * time.Second))
	http.ServeContent(w, r, found.Name, time.Time{}, f)
}

// Stream to a local clamd. Never shell out using a user's filename, or send files
// to a third-party scanning service. Only an exact OK verdict permits publication.
func scanFile(ctx context.Context, path string) error {
	if err := checkArchiveLimits(path); err != nil {
		return err
	}
	address := os.Getenv("NIMBO_CLAMD_ADDRESS")
	network := "tcp"
	if strings.HasPrefix(address, "unix:") {
		network = "unix"
		address = strings.TrimPrefix(address, "unix:")
	} else {
		host, _, err := net.SplitHostPort(address)
		if err != nil || !net.ParseIP(host).IsLoopback() {
			return fail(503, "Нужен локальный адрес ClamAV")
		}
	}
	conn, err := (&net.Dialer{Timeout: 3 * time.Second}).DialContext(ctx, network, address)
	if err != nil {
		return fail(503, "Антивирус недоступен. Файл не опубликован.")
	}
	defer conn.Close()
	stopCancel := context.AfterFunc(ctx, func() { _ = conn.Close() })
	defer stopCancel()
	deadline := time.Now().Add(65 * time.Second)
	if d, ok := ctx.Deadline(); ok && d.Before(deadline) {
		deadline = d
	}
	_ = conn.SetDeadline(deadline)
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	writer := bufio.NewWriter(conn)
	_, err = writer.WriteString("zINSTREAM\x00")
	buf := make([]byte, 32*1024)
	for err == nil {
		n, e := f.Read(buf)
		if n > 0 {
			err = binary.Write(writer, binary.BigEndian, uint32(n))
			if err == nil {
				_, err = writer.Write(buf[:n])
			}
		}
		if e == io.EOF {
			break
		}
		if e != nil {
			err = e
			break
		}
		if ctx.Err() != nil {
			err = ctx.Err()
			break
		}
	}
	if err == nil {
		err = binary.Write(writer, binary.BigEndian, uint32(0))
	}
	if err == nil {
		err = writer.Flush()
	}
	if err != nil {
		return fail(503, "Проверка файла не завершена")
	}
	verdict, err := bufio.NewReader(io.LimitReader(conn, 4096)).ReadString(0)
	if err != nil {
		return fail(503, "Проверка файла не завершена")
	}
	verdict = strings.TrimSuffix(verdict, "\x00")
	if verdict == "stream: OK" {
		return nil
	}
	if strings.Contains(verdict, "Heuristics.Limits.Exceeded.") {
		return fail(422, "Антивирус не успел полностью проверить файл или достиг лимита. Файл не опубликован. Попробуйте разделить пакет.")
	}
	if strings.HasSuffix(verdict, " FOUND") {
		return fail(422, "Антивирус обнаружил угрозу. Файл не опубликован.")
	}
	return fail(503, "Антивирус не смог проверить файл")
}

// Inspect ZIP's directory, never extract files or execute archive contents.
// ClamAV can skip oversized entries while returning OK; enforce our own limits.
func checkArchiveLimits(path string) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	var header [4]byte
	_, _ = f.ReadAt(header[:], 0)
	if header != [4]byte{'P', 'K', 3, 4} && header != [4]byte{'P', 'K', 5, 6} && header != [4]byte{'P', 'K', 7, 8} {
		return nil
	}
	info, err := f.Stat()
	if err != nil {
		return err
	}
	z, err := zip.NewReader(f, info.Size())
	if err != nil {
		return fail(422, "Архив не удалось проверить. Файл не опубликован.")
	}
	if len(z.File) > 1000 {
		return fail(422, "В архиве слишком много файлов.")
	}
	var total uint64
	for _, entry := range z.File {
		if entry.Flags&1 != 0 {
			return fail(422, "Зашифрованный архив нельзя проверить антивирусом.")
		}
		if entry.UncompressedSize64 > uint64(maxFileBytes) || entry.UncompressedSize64 > uint64(maxFileBytes)-total {
			return fail(422, "Распакованный архив превышает лимит 20 МБ.")
		}
		total += entry.UncompressedSize64
		ext := strings.ToLower(filepath.Ext(entry.Name))
		switch ext {
		case ".zip", ".rar", ".7z", ".gz", ".bz2", ".xz", ".tar", ".tgz":
			return fail(422, "Вложенные архивы не поддерживаются при антивирусной проверке.")
		}
	}
	return nil
}
