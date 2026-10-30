package main

import (
	"net/http"
	"strings"
	"time"
)

// A small local palette: no remote emoji requests or tracking.
const reactionPalette = "😀 😃 😄 😁 😆 😅 😂 🤣 🙂 😉 😊 😍 🥰 😘 😎 🤔 😮 😢 😭 😡 🤯 🥳 😴 🤝 👍 👎 👏 🙌 🙏 💪 👀 ❤️ 💔 🔥 ✨ 🎉 💯 ✅ ❌ 🚀 ☕ 🎮 🐈 🐕 🌈 🎵"

func (s *workspaceStore) chatHistory(w http.ResponseWriter, r *http.Request, id string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	ch := findChannel(&s.data, r.URL.Query().Get("channel"))
	if ch == nil || ch.Kind != "text" {
		reject(w, 404, "Текстовая комната недоступна")
		return
	}
	all := s.data.Messages[ch.ID]
	end := len(all)
	q := r.URL.Query()
	if before := q.Get("before"); before != "" {
		found := false
		for i, m := range all {
			if m.ID == before {
				end = i
				found = true
				break
			}
		}
		if !found {
			reject(w, 400, "Страница истории устарела")
			return
		}
	}
	start := max(0, end-100)
	if from := q.Get("from"); from != "" && q.Get("before") == "" {
		for i, m := range all {
			if m.ID == from {
				start = i
				break
			}
		}
	}
	page := make([]chatMessage, 0, end-start)
	for _, m := range all[start:end] {
		page = append(page, withReply(m, all))
	}
	if page == nil {
		page = []chatMessage{}
	}
	if q.Get("paged") == "1" {
		jsonReply(w, 200, map[string]any{"messages": page, "hasMore": start > 0})
	} else {
		jsonReply(w, 200, page)
	}
}

func (s *workspaceStore) react(w http.ResponseWriter, r *http.Request, id string) {
	var req struct {
		Channel string `json:"channel"`
		Emoji   string `json:"emoji"`
		Active  bool   `json:"active"`
	}
	if !decodeRequest(w, r, &req) {
		return
	}
	allowed := false
	for _, emoji := range strings.Fields(reactionPalette) {
		if req.Emoji == emoji {
			allowed = true
			break
		}
	}
	if !allowed {
		reject(w, 400, "Выберите эмодзи из списка")
		return
	}
	var result chatMessage
	err := s.update(func(data *workspaceData) error {
		if findMember(data, id).ChatMutedUntil > time.Now().UnixMilli() {
			return fail(403, "Возможность писать временно ограничена модератором")
		}
		ch := findChannel(data, req.Channel)
		if ch == nil || ch.Kind != "text" {
			return fail(404, "Текстовая комната недоступна")
		}
		for i := range data.Messages[ch.ID] {
			m := &data.Messages[ch.ID][i]
			if m.ID != r.PathValue("id") {
				continue
			}
			if m.Deleted {
				return fail(404, "Сообщение удалено")
			}
			if m.Reactions == nil {
				m.Reactions = map[string][]string{}
			}
			users := m.Reactions[req.Emoji]
			found := -1
			for n, user := range users {
				if user == id {
					found = n
					break
				}
			}
			if req.Active && found < 0 {
				users = append(users, id)
			} else if !req.Active && found >= 0 {
				users = append(users[:found], users[found+1:]...)
			}
			if len(users) == 0 {
				delete(m.Reactions, req.Emoji)
			} else {
				m.Reactions[req.Emoji] = users
			}
			result = withReply(*m, data.Messages[ch.ID])
			return nil
		}
		return fail(404, "Сообщение недоступно")
	})
	if err != nil {
		report(w, err)
		return
	}
	jsonReply(w, 200, result)
}

func (s *workspaceStore) deleteMessage(w http.ResponseWriter, r *http.Request, actor string) {
	var result chatMessage
	err := s.update(func(d *workspaceData) error {
		ch := findChannel(d, r.URL.Query().Get("channel"))
		if ch == nil || ch.Kind != "text" {
			return fail(404, "Комната недоступна")
		}
		for i := range d.Messages[ch.ID] {
			m := &d.Messages[ch.ID][i]
			if m.ID == r.PathValue("id") {
				if m.AuthorID != actor {
					if err := requirePermission(d, actor, "manage_chat"); err != nil {
						return err
					}
				}
				result = chatMessage{ID: m.ID, Deleted: true}
				d.Messages[ch.ID] = append(d.Messages[ch.ID][:i], d.Messages[ch.ID][i+1:]...)
				return nil
			}
		}
		return fail(404, "Сообщение не найдено")
	})
	if err != nil {
		report(w, err)
		return
	}
	jsonReply(w, 200, result)
}
