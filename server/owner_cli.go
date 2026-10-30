package main

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/pbkdf2"
	"crypto/rand"
	"crypto/sha256"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/url"
	"os"
	"strings"
	"unicode/utf16"
)

const profileAAD = "nimbo-encrypted-profile:v1:PBKDF2-SHA256:600000:AES-256-GCM"

type profileServer struct {
	URL         string `json:"url"`
	Name        string `json:"name"`
	DisplayName string `json:"displayName"`
	ProfileKey  string `json:"profileKey"`
}
type portableProfile struct {
	Format  string          `json:"format"`
	Version int             `json:"version"`
	Name    string          `json:"name"`
	Avatar  string          `json:"avatar"`
	Servers []profileServer `json:"servers"`
}
type encryptedProfile struct {
	Format     string `json:"format"`
	Version    int    `json:"version"`
	KDF        string `json:"kdf"`
	Iterations int    `json:"iterations"`
	Cipher     string `json:"cipher"`
	Salt       []byte `json:"salt"`
	IV         []byte `json:"iv"`
	Data       []byte `json:"data"`
}

func profileCipher(password string, salt []byte) (cipher.AEAD, error) {
	if len(utf16.Encode([]rune(password))) < 12 || len(utf16.Encode([]rune(password))) > 256 {
		return nil, errors.New("file password must contain 12..256 characters")
	}
	key, err := pbkdf2.Key(sha256.New, password, salt, 600000, 32)
	if err != nil {
		return nil, err
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	return cipher.NewGCM(block)
}
func sealProfile(p portableProfile, password string) ([]byte, error) {
	e := encryptedProfile{Format: "nimbo-encrypted-profile", Version: 1, KDF: "PBKDF2-SHA256", Iterations: 600000, Cipher: "AES-256-GCM", Salt: make([]byte, 16), IV: make([]byte, 12)}
	if _, err := rand.Read(e.Salt); err != nil {
		return nil, err
	}
	if _, err := rand.Read(e.IV); err != nil {
		return nil, err
	}
	a, err := profileCipher(password, e.Salt)
	if err != nil {
		return nil, err
	}
	plain, err := json.Marshal(p)
	if err != nil {
		return nil, err
	}
	e.Data = a.Seal(nil, e.IV, plain, []byte(profileAAD))
	return json.Marshal(e)
}
func unsealProfile(raw []byte, password string) (portableProfile, error) {
	var e encryptedProfile
	var p portableProfile
	if len(raw) > 300000 || json.Unmarshal(raw, &e) != nil || e.Format != "nimbo-encrypted-profile" || e.Version != 1 || e.KDF != "PBKDF2-SHA256" || e.Iterations != 600000 || e.Cipher != "AES-256-GCM" || len(e.Salt) != 16 || len(e.IV) != 12 || len(e.Data) < 17 || len(e.Data) > 200016 {
		return p, errors.New("unsupported encrypted profile")
	}
	a, err := profileCipher(password, e.Salt)
	if err != nil {
		return p, err
	}
	plain, err := a.Open(nil, e.IV, e.Data, []byte(profileAAD))
	if err != nil {
		return p, errors.New("incorrect file password or damaged profile")
	}
	if json.Unmarshal(plain, &p) != nil || p.Format != "nimbo-profile" || p.Version != 1 || !validLabel(p.Name, 40) || !validAvatar(p.Avatar) || len(p.Servers) > 64 {
		return p, errors.New("invalid profile")
	}
	return p, nil
}
func (s *workspaceStore) assignOwner(id string, transfer bool) error {
	return s.update(func(d *workspaceData) error {
		m := findMember(d, id)
		if m == nil || isBlocked(m) {
			return errors.New("active member not found")
		}
		for _, other := range d.Members {
			if other.Role == "owner" && other.ID != id && !transfer {
				return errors.New("owner exists; explicit --transfer required")
			}
		}
		for i := range d.Members {
			if d.Members[i].Role == "owner" && d.Members[i].ID != id {
				d.Members[i].Role = "admin"
				d.Members[i].RoleIDs = []string{"admin"}
			}
		}
		m.Role = "owner"
		return nil
	})
}
func boundedRead(path string, max int64) ([]byte, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	b, err := io.ReadAll(io.LimitReader(f, max+1))
	if int64(len(b)) > max {
		return nil, errors.New("input file too large")
	}
	return b, err
}

// Run only while the API process is stopped: the workspace has one writer.
func runOwnerCommand(args []string) error {
	flags := flag.NewFlagSet("owner", flag.ContinueOnError)
	data := flags.String("data", os.Getenv("NIMBO_DATA_FILE"), "workspace.json path")
	id := flags.String("member", "", "existing member ID")
	create := flags.String("create", "", "new owner's name")
	input := flags.String("import", "", "encrypted .nimbo profile file")
	output := flags.String("export", "", "new encrypted profile output (must not exist)")
	passwordFile := flags.String("password-file", "", "local file containing the export/import password")
	origin := flags.String("origin", os.Getenv("NIMBO_ORIGIN"), "server origin matching the browser URL")
	transfer := flags.Bool("transfer", false, "explicitly transfer ownership; previous owner becomes admin")
	offline := flags.Bool("offline", false, "confirm the API service is stopped")
	if err := flags.Parse(args); err != nil {
		return err
	}
	if !*offline {
		return errors.New("stop the API first, then pass --offline")
	}
	if *data == "" {
		return errors.New("--data required")
	}
	count := 0
	for _, value := range []string{*id, *create, *input} {
		if value != "" {
			count++
		}
	}
	if count != 1 {
		return errors.New("choose exactly one: --member, --create, --import")
	}
	s, err := openWorkspace(*data, "")
	if err != nil {
		return err
	}
	if *id != "" {
		return s.assignOwner(*id, *transfer)
	}
	u, err := url.Parse(*origin)
	if err != nil || u == nil || u.Host == "" || u.User != nil || u.RawQuery != "" || u.Fragment != "" || u.Path != "" || u.Scheme != "https" && !(u.Scheme == "http" && (u.Hostname() == "localhost" || u.Hostname() == "127.0.0.1" || u.Hostname() == "::1")) {
		return errors.New("valid --origin required")
	}
	passwordBytes, err := boundedRead(*passwordFile, 1024)
	if err != nil {
		return errors.New("cannot read --password-file")
	}
	password := strings.TrimRight(string(passwordBytes), "\r\n")
	var p portableProfile
	var key, name string
	if *input != "" {
		raw, err := boundedRead(*input, 300000)
		if err != nil {
			return err
		}
		p, err = unsealProfile(raw, password)
		if err != nil {
			return err
		}
		for _, server := range p.Servers {
			if server.URL == *origin {
				if key != "" {
					return errors.New("duplicate server entry")
				}
				key = server.ProfileKey
				name = server.DisplayName
			}
		}
		if key == "" {
			if len(p.Servers) >= 64 {
				return errors.New("profile server limit reached")
			}
			key = randomKey()
			name = p.Name
			p.Servers = append(p.Servers, profileServer{*origin, s.data.Name, name, key})
			if *output == "" {
				return errors.New("importing into a new server requires --export for the updated profile")
			}
		}
	} else {
		name = strings.TrimSpace(*create)
		key = randomKey()
		p = portableProfile{Format: "nimbo-profile", Version: 1, Name: name, Servers: []profileServer{{*origin, s.data.Name, name, key}}}
	}
	if !validKey(key) || !validLabel(name, 40) {
		return errors.New("invalid profile identity")
	}
	hash := keyHash(key)
	target := member{ID: randomKey(), Name: name, Role: "owner", KeyHash: hash, Avatar: p.Avatar}
	if target.Avatar != "" {
		target.AvatarVersion = keyHash(target.Avatar)
	}
	for _, m := range s.data.Members {
		if m.KeyHash == hash {
			target = m
			break
		}
	}
	if isBlocked(&target) {
		return errors.New("profile is blocked")
	}
	for _, m := range s.data.Members {
		if m.Role == "owner" && m.ID != target.ID && !*transfer {
			return errors.New("owner exists; explicit --transfer required")
		}
	}
	wrote := false
	if *create != "" || *output != "" {
		if *output == "" {
			return errors.New("new owner requires --export")
		}
		raw, err := sealProfile(p, password)
		if err != nil {
			return err
		}
		f, err := os.OpenFile(*output, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
		if err != nil {
			return err
		}
		_, err = f.Write(raw)
		if err == nil {
			err = f.Sync()
		}
		closeErr := f.Close()
		if err == nil {
			err = closeErr
		}
		if err != nil {
			os.Remove(*output)
			return err
		}
		wrote = true
	}
	err = s.update(func(d *workspaceData) error {
		for i := range d.Members {
			if d.Members[i].Role == "owner" && d.Members[i].ID != target.ID {
				d.Members[i].Role = "admin"
				d.Members[i].RoleIDs = []string{"admin"}
			}
		}
		if m := findMember(d, target.ID); m != nil {
			m.Role = "owner"
		} else {
			if len(d.Members) >= 1000 {
				return errors.New("member limit reached")
			}
			d.Members = append(d.Members, target)
		}
		return nil
	})
	if err != nil && wrote {
		os.Remove(*output)
	}
	if err == nil {
		fmt.Println("Owner configured. Keep the encrypted profile and its password separately.")
	}
	return err
}
