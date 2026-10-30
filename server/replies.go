package main

type messageQuote struct {
	ID      string `json:"id"`
	Name    string `json:"name"`
	Text    string `json:"text"`
	Deleted bool   `json:"deleted,omitempty"`
}

// Quotes are resolved at read time so deleting an original never leaves copies of its text.
func withReply(m chatMessage, messages []chatMessage) chatMessage {
	m.MentionUnread = nil
	m.Reply = nil
	if m.ReplyTo == "" || m.Deleted {
		return m
	}
	m.Reply = &messageQuote{ID: m.ReplyTo, Deleted: true}
	for _, original := range messages {
		if original.ID == m.ReplyTo {
			m.Reply = &messageQuote{ID: original.ID, Name: original.Name, Deleted: original.Deleted}
			if !original.Deleted {
				r := []rune(original.Text)
				if len(r) > 180 {
					r = r[:180]
				}
				m.Reply.Text = string(r)
			}
			break
		}
	}
	return m
}
