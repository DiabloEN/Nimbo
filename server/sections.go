package main

import (
	"net/http"
	"strings"
)

type channelSection struct {
	ID   string `json:"id"`
	Name string `json:"name"`
	Kind string `json:"kind"`
}

func sectionsFor(d *workspaceData) []channelSection {
	if len(d.Sections) > 0 {
		return d.Sections
	}
	return []channelSection{{"voice", "Голосовые", "voice"}, {"text", "Текстовые", "text"}}
}
func ensureSections(d *workspaceData) {
	d.Sections = sectionsFor(d)
	if d.ChannelSections == nil {
		d.ChannelSections = map[string]string{}
	}
	for _, c := range d.Channels {
		if d.ChannelSections[c.ID] == "" {
			d.ChannelSections[c.ID] = c.Kind
		}
	}
}
func findSection(d *workspaceData, id string) *channelSection {
	for i := range d.Sections {
		if d.Sections[i].ID == id {
			return &d.Sections[i]
		}
	}
	return nil
}
func assignSection(d *workspaceData, c channel, id string) error {
	ensureSections(d)
	if id == "" {
		for _, s := range d.Sections {
			if s.Kind == c.Kind {
				id = s.ID
				break
			}
		}
	}
	s := findSection(d, id)
	if s == nil || s.Kind != c.Kind {
		return fail(400, "Тип канала должен совпадать с типом секции")
	}
	d.ChannelSections[c.ID] = id
	return nil
}
func (s *workspaceStore) registerSections(handle func(string, func(http.ResponseWriter, *http.Request, string))) {
	mutate := func(w http.ResponseWriter, r *http.Request, actor string) {
		var req struct {
			Name string `json:"name"`
			Kind string `json:"kind"`
		}
		if r.Method != "DELETE" && !decodeRequest(w, r, &req) {
			return
		}
		err := s.update(func(d *workspaceData) error {
			if e := requireManager(d, actor, false); e != nil {
				return e
			}
			ensureSections(d)
			id := r.PathValue("id")
			if r.Method == "DELETE" {
				if findSection(d, id) == nil {
					return fail(404, "Секция не найдена")
				}
				if len(d.Sections) == 1 {
					return fail(409, "Оставьте хотя бы одну секцию")
				}
				for _, c := range d.Channels {
					if !c.Archived && d.ChannelSections[c.ID] == id {
						return fail(409, "Сначала перенесите или удалите каналы секции")
					}
				}
				for i, v := range d.Sections {
					if v.ID == id {
						d.Sections = append(d.Sections[:i], d.Sections[i+1:]...)
						break
					}
				}
				order := []string{}
				for _, v := range d.SectionOrder {
					if v != id {
						order = append(order, v)
					}
				}
				d.SectionOrder = order
				return nil
			}
			req.Name = strings.TrimSpace(req.Name)
			if !validLabel(req.Name, 60) {
				return fail(400, "Название секции: 1–60 символов")
			}
			if r.Method == "POST" {
				if len(d.Sections) >= 32 {
					return fail(409, "Можно создать до 32 секций")
				}
				if req.Kind != "voice" && req.Kind != "text" {
					return fail(400, "Выберите тип секции")
				}
				order := sectionOrder(d)
				v := channelSection{randomKey(), req.Name, req.Kind}
				d.Sections = append(d.Sections, v)
				d.SectionOrder = append(order, v.ID)
				return nil
			}
			v := findSection(d, id)
			if v == nil {
				return fail(404, "Секция не найдена")
			}
			v.Name = req.Name
			return nil
		})
		if err != nil {
			report(w, err)
			return
		}
		jsonReply(w, 200, map[string]bool{"ok": true})
	}
	handle("POST /api/sections", mutate)
	handle("PATCH /api/sections/{id}", mutate)
	handle("DELETE /api/sections/{id}", mutate)
}
