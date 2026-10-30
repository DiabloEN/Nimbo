package main

import (
	"net/http"
	"strings"
	"time"
	"unicode"
	"unicode/utf16"
	"unicode/utf8"
)

func (s *workspaceStore) editMessage(w http.ResponseWriter, r *http.Request, actor string) {
	var req struct {
		Channel      string        `json:"channel"`
		Text         string        `json:"text"`
		PreviousText *string       `json:"previousText"`
		Mentions     []chatMention `json:"mentions"`
	}
	if !decodeRequest(w, r, &req) {
		return
	}
	leading := len(utf16.Encode([]rune(req.Text))) - len(utf16.Encode([]rune(strings.TrimLeftFunc(req.Text, unicode.IsSpace))))
	for i := range req.Mentions {
		req.Mentions[i].Start -= leading
		req.Mentions[i].End -= leading
	}
	req.Text = strings.TrimSpace(req.Text)
	if !utf8.ValidString(req.Text) || len(req.Text) > 32000 || utf8.RuneCountInString(req.Text) > 8000 || strings.IndexFunc(req.Text, func(r rune) bool { return unicode.IsControl(r) && r != '\n' && r != '\t' }) >= 0 {
		reject(w, 400, "Сообщение: до 8000 символов")
		return
	}
	var result chatMessage
	err := s.update(func(d *workspaceData) error {
		ch := findChannel(d, req.Channel)
		if ch == nil || ch.Kind != "text" {
			return fail(404, "Комната недоступна")
		}
		if err := requireChannelWriter(d, ch, actor); err != nil {
			return err
		}
		if findMember(d, actor).ChatMutedUntil > time.Now().UnixMilli() {
			return fail(403, "Возможность писать временно ограничена модератором")
		}
		for i := range d.Messages[ch.ID] {
			m := &d.Messages[ch.ID][i]
			if m.ID != r.PathValue("id") {
				continue
			}
			if m.Deleted {
				return fail(404, "Сообщение удалено")
			}
			if m.AuthorID != actor {
				return fail(403, "Можно изменять только свои сообщения")
			}
			if req.PreviousText == nil || *req.PreviousText != m.Text {
				return fail(409, "Сообщение уже изменилось. Откройте редактирование заново.")
			}
			if req.Text == "" && m.Attachment == nil && len(m.Attachments) == 0 {
				return fail(400, "Введите текст сообщения")
			}
			mentions, _, err := resolveMentions(d, actor, req.Text, req.Mentions)
			if err != nil {
				return err
			}
			if m.Text != req.Text {
				m.Text = req.Text
				m.Mentions = mentions
				m.EditedAt = time.Now().UnixMilli()
			}
			// Editing does not generate a second mention/reply notification.
			result = withReply(*m, d.Messages[ch.ID])
			return nil
		}
		return fail(404, "Сообщение не найдено")
	})
	if err != nil {
		report(w, err)
		return
	}
	jsonReply(w, 200, result)
}
