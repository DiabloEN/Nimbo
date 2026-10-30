package main

import (
	"net/http"
	"slices"
	"sort"
	"unicode/utf16"
)

// Offsets use UTF-16 code units, matching textarea selections in the browser.
type chatMention struct {
	Kind  string `json:"kind"`
	ID    string `json:"id"`
	Label string `json:"label"`
	Start int    `json:"start"`
	End   int    `json:"end"`
}
type mentionNotice struct {
	ID          string `json:"id"`
	Channel     string `json:"channel"`
	ChannelName string `json:"channelName"`
	Name        string `json:"name"`
	Text        string `json:"text"`
	CreatedAt   int64  `json:"createdAt"`
}

type chatActivity struct {
	ID        string `json:"id"`
	CreatedAt int64  `json:"createdAt"`
}

func activityFor(d *workspaceData, id string) map[string]chatActivity {
	activity := map[string]chatActivity{}
	for _, ch := range d.Channels {
		if ch.Archived || ch.Kind != "text" {
			continue
		}
		messages := d.Messages[ch.ID]
		for i := len(messages) - 1; i >= 0; i-- {
			m := messages[i]
			if !m.Deleted && m.AuthorID != id {
				activity[ch.ID] = chatActivity{m.ID, m.CreatedAt}
				break
			}
		}
	}
	return activity
}

func resolveMentions(d *workspaceData, actor, text string, mentions []chatMention) ([]chatMention, []string, error) {
	if len(mentions) > 50 {
		return nil, nil, fail(400, "Не более 50 упоминаний в сообщении")
	}
	units := utf16.Encode([]rune(text))
	mentions = append([]chatMention{}, mentions...)
	sort.Slice(mentions, func(i, j int) bool { return mentions[i].Start < mentions[j].Start })
	recipients := []string{}
	last := 0
	for i := range mentions {
		m := &mentions[i]
		label := ""
		switch m.Kind {
		case "everyone":
			if m.ID != "everyone" {
				return nil, nil, fail(400, "Неизвестное упоминание")
			}
			label = "everyone"
		case "member":
			target := findMember(d, m.ID)
			if target == nil || isBlocked(target) {
				return nil, nil, fail(400, "Участник недоступен")
			}
			label = target.Name
		case "role":
			role := roleFor(d, m.ID)
			if m.ID == "owner" || role == nil {
				return nil, nil, fail(400, "Эту роль нельзя упомянуть")
			}
			if (m.ID == "admin" || slices.Contains(role.Permissions, "manage_roles")) && findMember(d, actor).Role != "owner" {
				return nil, nil, fail(403, "Администраторов может упоминать только владелец")
			}
			label = role.Name
		default:
			return nil, nil, fail(400, "Неизвестное упоминание")
		}
		if m.Start < last || m.Start < 0 || m.End <= m.Start || m.End > len(units) || string(utf16.Decode(units[m.Start:m.End])) != "@"+label {
			return nil, nil, fail(400, "Упоминание изменилось. Выберите его заново через @")
		}
		m.Label = label
		last = m.End
		for n := range d.Members {
			target := &d.Members[n]
			if isBlocked(target) || target.ID == actor {
				continue
			}
			matches := m.Kind == "everyone" || m.Kind == "member" && target.ID == m.ID
			if m.Kind == "role" && target.Role != "owner" {
				matches = (m.ID == "member" && target.Role == "member") || slices.Contains(memberRoles(target), m.ID)
			}
			if matches && !slices.Contains(recipients, target.ID) {
				recipients = append(recipients, target.ID)
			}
		}
	}
	return mentions, recipients, nil
}
func mentionNotices(d *workspaceData, id string) []mentionNotice {
	notices := []mentionNotice{}
	for _, ch := range d.Channels {
		if ch.Archived || ch.Kind != "text" {
			continue
		}
		for _, m := range d.Messages[ch.ID] {
			if !m.Deleted && slices.Contains(m.MentionUnread, id) {
				notices = append(notices, mentionNotice{m.ID, ch.ID, ch.Name, m.Name, m.Text, m.CreatedAt})
			}
		}
	}
	sort.Slice(notices, func(i, j int) bool {
		if notices[i].CreatedAt == notices[j].CreatedAt {
			return notices[i].ID > notices[j].ID
		}
		return notices[i].CreatedAt > notices[j].CreatedAt
	})
	return notices[:min(len(notices), 50)]
}
func (s *workspaceStore) readMentions(w http.ResponseWriter, r *http.Request, id string) {
	var req struct {
		IDs []string `json:"ids"`
	}
	if !decodeRequest(w, r, &req) {
		return
	}
	if len(req.IDs) > 50 {
		reject(w, 400, "Слишком много уведомлений")
		return
	}
	err := s.update(func(d *workspaceData) error {
		for ch, messages := range d.Messages {
			for i := range messages {
				if slices.Contains(req.IDs, messages[i].ID) {
					messages[i].MentionUnread = slices.DeleteFunc(messages[i].MentionUnread, func(target string) bool { return target == id })
				}
			}
			d.Messages[ch] = messages
		}
		return nil
	})
	if err != nil {
		report(w, err)
		return
	}
	jsonReply(w, 200, map[string]bool{"ok": true})
}

// Replies share the unread inbox with mentions, once per message and recipient.
func replyRecipients(d *workspaceData, messages []chatMessage, actor, replyTo string, recipients []string) []string {
	if replyTo == "" {
		return recipients
	}
	for _, original := range messages {
		if original.ID != replyTo || original.Deleted {
			continue
		}
		target := findMember(d, original.AuthorID)
		if target != nil && !isBlocked(target) && target.ID != actor && !slices.Contains(recipients, target.ID) {
			recipients = append(recipients, target.ID)
		}
		break
	}
	return recipients
}
