package main

import "net/http"

func sectionOrder(d *workspaceData) []string {
	defs := sectionsFor(d)
	out := []string{}
	seen := map[string]bool{}
	for _, id := range d.SectionOrder {
		for _, sec := range defs {
			if sec.ID == id && !seen[id] {
				out = append(out, id)
				seen[id] = true
			}
		}
	}
	for _, sec := range defs {
		if !seen[sec.ID] {
			out = append(out, sec.ID)
		}
	}
	return out
}
func (s *workspaceStore) reorderChannels(w http.ResponseWriter, r *http.Request, id string) {
	var req struct {
		Sections []string `json:"sections"`
		Channels []string `json:"channels"`
	}
	if !decodeRequest(w, r, &req) {
		return
	}
	err := s.update(func(d *workspaceData) error {
		if err := requireManager(d, id, false); err != nil {
			return err
		}
		ensureSections(d)
		if len(req.Sections) != len(d.Sections) {
			return fail(409, "Список секций изменился")
		}
		seen := map[string]bool{}
		for _, key := range req.Sections {
			if findSection(d, key) == nil || seen[key] {
				return fail(400, "Некорректный порядок секций")
			}
			seen[key] = true
		}
		active := map[string]channel{}
		archived := []channel{}
		for _, ch := range d.Channels {
			if ch.Archived {
				archived = append(archived, ch)
			} else {
				active[ch.ID] = ch
			}
		}
		if len(req.Channels) != len(active) {
			return fail(409, "Список каналов изменился. Обновите настройки.")
		}
		ordered := []channel{}
		for _, key := range req.Channels {
			ch, ok := active[key]
			if !ok {
				return fail(409, "Список каналов изменился. Обновите настройки.")
			}
			delete(active, key)
			ordered = append(ordered, ch)
		}
		d.Channels = append(ordered, archived...)
		d.SectionOrder = append([]string{}, req.Sections...)
		return nil
	})
	if err != nil {
		report(w, err)
		return
	}
	jsonReply(w, 200, map[string]bool{"ok": true})
}
