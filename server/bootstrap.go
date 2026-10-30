package main

import (
	"crypto/hmac"
	"errors"
	"flag"
	"fmt"
	"os"
	"time"
)

// Only a hash of the short-lived console code is stored. It is distinct from
// ordinary invitations and can never transfer an already owned workspace.
type ownerBootstrap struct {
	Hash      string `json:"hash"`
	ExpiresAt int64  `json:"expiresAt"`
}

func hasOwner(d *workspaceData) bool {
	for _, m := range d.Members {
		if m.Role == "owner" {
			return true
		}
	}
	return false
}

func claimBootstrap(d *workspaceData, name, key, code string) (member, bool, error) {
	b := d.OwnerBootstrap
	if b == nil || code == "" || !hmac.Equal([]byte(b.Hash), []byte(keyHash(code))) {
		return member{}, false, nil
	}
	if hasOwner(d) || b.ExpiresAt <= time.Now().UnixMilli() {
		return member{}, true, fail(401, "Код владельца недействителен или истёк")
	}
	hash := keyHash(key)
	for i := range d.Members {
		m := &d.Members[i]
		if hmac.Equal([]byte(m.KeyHash), []byte(hash)) {
			if isBlocked(m) {
				return member{}, true, fail(403, "Профиль заблокирован")
			}
			m.Role = "owner"
			d.OwnerBootstrap = nil
			return *m, true, nil
		}
	}
	if len(d.Members) >= 1000 {
		return member{}, true, fail(409, "Достигнут лимит участников")
	}
	m := member{ID: randomKey(), Name: name, Role: "owner", KeyHash: hash}
	d.Members = append(d.Members, m)
	d.OwnerBootstrap = nil
	return m, true, nil
}

// The service wrapper stops the API before calling this command. The JSON store
// remains single-writer; do not invoke --offline against a running API.
func runBootstrapCommand(command string, args []string) error {
	f := flag.NewFlagSet(command, flag.ContinueOnError)
	path := f.String("data", os.Getenv("NIMBO_DATA_FILE"), "workspace.json path")
	offline := f.Bool("offline", false, "API must be stopped")
	if err := f.Parse(args); err != nil {
		return err
	}
	if !*offline || *path == "" || f.NArg() != 0 {
		return errors.New("provide --offline and --data (or NIMBO_DATA_FILE); stop the API first")
	}
	_, statErr := os.Stat(*path)
	if command == "init" {
		if !errors.Is(statErr, os.ErrNotExist) {
			return errors.New("workspace already exists or cannot be inspected; refusing to initialize")
		}
	} else if statErr != nil {
		return errors.New("initialize the workspace before generating an owner code")
	}
	openPath := *path
	if command == "init" {
		openPath = ""
	}
	s, err := openWorkspace(openPath, "")
	if err != nil {
		return err
	}
	if command == "init" {
		s.path = *path
		s.data.InvitationsReady = true
		if err := s.persist(s.data); err != nil {
			return err
		}
		fmt.Println("Workspace initialized. Enrollment is closed. Run nimbo owner-code when ready.")
		return nil
	}
	code := randomKey()
	expires := time.Now().Add(30 * time.Minute)
	err = s.update(func(d *workspaceData) error {
		if hasOwner(d) {
			return errors.New("owner already exists; this command cannot transfer ownership")
		}
		d.InvitationsReady = true
		d.OwnerBootstrap = &ownerBootstrap{Hash: keyHash(code), ExpiresAt: expires.UnixMilli()}
		return nil
	})
	if err != nil {
		return err
	}
	fmt.Println("Одноразовый код владельца (30 минут):")
	fmt.Println(code)
	fmt.Println("Введите его как код приглашения при добавлении этого сервера в Nimbo.")
	fmt.Println("Повторный вызов owner-code отменяет предыдущий неиспользованный код.")
	return nil
}
