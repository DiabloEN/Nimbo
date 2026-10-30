package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestEncryptedOwnerSetupAndTransfer(t *testing.T) {
	dir := t.TempDir()
	data := filepath.Join(dir, "workspace.json")
	password := filepath.Join(dir, "password")
	output := filepath.Join(dir, "owner.nimbo")
	os.WriteFile(password, []byte("local-test-password-123"), 0600)
	args := []string{"--data", data, "--offline", "--origin", "https://test.example", "--create", "Owner", "--password-file", password, "--export", output}
	if err := runOwnerCommand(args); err != nil {
		t.Fatal(err)
	}
	raw, _ := os.ReadFile(output)
	profile, err := unsealProfile(raw, "local-test-password-123")
	if err != nil || profile.Name != "Owner" {
		t.Fatal("cannot decrypt owner profile", err)
	}
	if _, err := unsealProfile(raw, "wrong-password-value"); err == nil {
		t.Fatal("wrong password accepted")
	}
	var envelope encryptedProfile
	json.Unmarshal(raw, &envelope)
	envelope.Data[0] ^= 1
	broken, _ := json.Marshal(envelope)
	if _, err := unsealProfile(broken, "local-test-password-123"); err == nil {
		t.Fatal("tampered profile accepted")
	}
	s, _ := openWorkspace(data, "")
	owner := s.data.Members[0]
	if owner.Role != "owner" || owner.KeyHash != keyHash(profile.Servers[0].ProfileKey) {
		t.Fatal("owner not bound to profile")
	}
	if err := runOwnerCommand(args); err == nil {
		t.Fatal("existing owner replaced without transfer")
	}
	member, _, err := s.login("Next", randomKey())
	if err != nil {
		t.Fatal(err)
	}
	if err := s.assignOwner(member.ID, false); err == nil {
		t.Fatal("transfer needs explicit flag")
	}
	if err := s.assignOwner(member.ID, true); err != nil {
		t.Fatal(err)
	}
	if findMember(&s.data, owner.ID).Role != "admin" {
		t.Fatal("former owner not demoted")
	}
	// Import into a different server preserves the profile and adds that membership.
	otherData := filepath.Join(dir, "other.json")
	otherProfile := filepath.Join(dir, "other.nimbo")
	if err := runOwnerCommand([]string{"--offline", "--data", otherData, "--origin", "https://other.example", "--import", output, "--password-file", password, "--export", otherProfile}); err != nil {
		t.Fatal(err)
	}
	raw, _ = os.ReadFile(otherProfile)
	restored, err := unsealProfile(raw, "local-test-password-123")
	if err != nil || len(restored.Servers) != 2 {
		t.Fatal("import discarded existing memberships", err)
	}
}
