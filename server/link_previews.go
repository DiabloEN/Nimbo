package main

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"image"
	_ "image/jpeg"
	_ "image/png"
	"io"
	"net"
	"net/http"
	"net/netip"
	"net/url"
	"regexp"
	"strings"
	"sync"
	"time"
	"unicode/utf8"

	"golang.org/x/net/html"
)

type linkCard struct {
	Title       string `json:"title,omitempty"`
	Description string `json:"description,omitempty"`
	Image       string `json:"image,omitempty"`
	Price       string `json:"price,omitempty"`
}
type cachedCard struct {
	card  linkCard
	until time.Time
}

var reservedNetworks = []netip.Prefix{
	netip.MustParsePrefix("0.0.0.0/8"), netip.MustParsePrefix("100.64.0.0/10"),
	netip.MustParsePrefix("192.0.0.0/24"), netip.MustParsePrefix("192.0.2.0/24"),
	netip.MustParsePrefix("192.88.99.0/24"), netip.MustParsePrefix("198.18.0.0/15"),
	netip.MustParsePrefix("198.51.100.0/24"), netip.MustParsePrefix("203.0.113.0/24"), netip.MustParsePrefix("240.0.0.0/4"),
}

func publicPreviewIP(ip netip.Addr) bool {
	if !ip.Is4() || !ip.IsGlobalUnicast() || ip.IsPrivate() {
		return false
	}
	for _, p := range reservedNetworks {
		if p.Contains(ip) {
			return false
		}
	}
	return true
}
func validPreviewURL(u *url.URL) bool {
	return u != nil && u.Scheme == "https" && u.User == nil && u.Hostname() != "" && (u.Port() == "" || u.Port() == "443") && len(u.String()) <= 2048
}
func previewHTTP() *http.Client {
	transport := &http.Transport{DisableKeepAlives: true, MaxResponseHeaderBytes: 32768, TLSHandshakeTimeout: 3 * time.Second}
	transport.DialContext = func(ctx context.Context, _, addr string) (net.Conn, error) {
		host, port, err := net.SplitHostPort(addr)
		if err != nil || port != "443" {
			return nil, errors.New("invalid destination")
		}
		// Dial the validated numeric address directly, including after each redirect.
		ips, err := net.DefaultResolver.LookupNetIP(ctx, "ip4", host)
		if err != nil {
			return nil, err
		}
		for _, ip := range ips {
			if !publicPreviewIP(ip.Unmap()) {
				return nil, errors.New("non-public destination")
			}
		}
		for _, ip := range ips {
			c, e := (&net.Dialer{Timeout: 2 * time.Second}).DialContext(ctx, "tcp4", net.JoinHostPort(ip.String(), port))
			if e == nil {
				return c, nil
			}
			err = e
		}
		if err == nil {
			err = errors.New("no address")
		}
		return nil, err
	}
	return &http.Client{Transport: transport, Timeout: 6 * time.Second, CheckRedirect: func(req *http.Request, prior []*http.Request) error {
		if len(prior) > 3 || !validPreviewURL(req.URL) {
			return errors.New("unsupported redirect")
		}
		req.Header.Del("Referer")
		return nil
	}}
}
func getPreview(ctx context.Context, client *http.Client, u *url.URL, limit int64, prefixOnly bool) ([]byte, *url.URL, error) {
	if !validPreviewURL(u) {
		return nil, nil, errors.New("unsupported URL")
	}
	req, err := http.NewRequestWithContext(ctx, "GET", u.String(), nil)
	if err != nil {
		return nil, nil, err
	}
	req.Header.Set("User-Agent", "Nimbo-LinkPreview/1")
	res, err := client.Do(req)
	if err != nil {
		return nil, nil, err
	}
	defer res.Body.Close()
	if res.StatusCode != 200 {
		return nil, nil, errors.New("unavailable")
	}
	body, err := io.ReadAll(io.LimitReader(res.Body, limit+1))
	if int64(len(body)) > limit {
		if prefixOnly {
			return body[:limit], res.Request.URL, err
		}
		return nil, nil, errors.New("too large")
	}
	return body, res.Request.URL, err
}
func shortText(value string, limit int) string {
	value = strings.Join(strings.Fields(value), " ")
	if utf8.RuneCountInString(value) > limit {
		return string([]rune(value)[:limit])
	}
	return value
}
func product(node any, depth int) (linkCard, string) {
	if depth > 12 {
		return linkCard{}, ""
	}
	switch v := node.(type) {
	case []any:
		for _, child := range v {
			if c, img := product(child, depth+1); c.Title != "" {
				return c, img
			}
		}
	case map[string]any:
		if graph, ok := v["@graph"]; ok {
			if c, img := product(graph, depth+1); c.Title != "" {
				return c, img
			}
		}
		types := v["@type"]
		isProduct := types == "Product"
		if list, ok := types.([]any); ok {
			for _, t := range list {
				if t == "Product" {
					isProduct = true
				}
			}
		}
		if !isProduct {
			return linkCard{}, ""
		}
		name, _ := v["name"].(string)
		desc, _ := v["description"].(string)
		raw := v["image"]
		if a, ok := raw.([]any); ok && len(a) > 0 {
			raw = a[0]
		}
		if obj, ok := raw.(map[string]any); ok {
			raw = obj["url"]
		}
		img, _ := raw.(string)
		c := linkCard{Title: name, Description: desc}
		offer := v["offers"]
		if a, ok := offer.([]any); ok && len(a) > 0 {
			offer = a[0]
		}
		if o, ok := offer.(map[string]any); ok {
			currency, _ := o["priceCurrency"].(string)
			switch p := o["price"].(type) {
			case string:
				c.Price = p + " " + currency
			case float64:
				c.Price = fmt.Sprint(p) + " " + currency
			}
		}
		return c, img
	}
	return linkCard{}, ""
}
func parsePreview(body []byte) (linkCard, string) {
	z := html.NewTokenizer(bytes.NewReader(body))
	meta := map[string]string{}
	var schema linkCard
	var schemaImage string
	for {
		event := z.Next()
		if event == html.ErrorToken {
			break
		}
		if event != html.StartTagToken && event != html.SelfClosingTagToken {
			continue
		}
		tok := z.Token()
		attrs := map[string]string{}
		for _, a := range tok.Attr {
			attrs[strings.ToLower(a.Key)] = a.Val
		}
		if tok.Data == "meta" {
			key := strings.ToLower(attrs["property"])
			if key == "" {
				key = strings.ToLower(attrs["name"])
			}
			if meta[key] == "" {
				meta[key] = attrs["content"]
			}
		}
		if tok.Data == "script" && strings.EqualFold(strings.TrimSpace(attrs["type"]), "application/ld+json") && schema.Title == "" && z.Next() == html.TextToken {
			var node any
			if json.Unmarshal(z.Text(), &node) == nil {
				schema, schemaImage = product(node, 0)
			}
		}
	}
	if schema.Title == "" {
		schema.Title = meta["og:title"]
	}
	if schema.Title == "" {
		schema.Title = meta["twitter:title"]
	}
	if schema.Description == "" {
		schema.Description = meta["og:description"]
	}
	if schemaImage == "" {
		schemaImage = meta["og:image"]
	}
	if schemaImage == "" {
		schemaImage = meta["twitter:image"]
	}
	schema.Title = shortText(schema.Title, 140)
	schema.Description = shortText(schema.Description, 220)
	schema.Price = shortText(schema.Price, 40)
	return schema, schemaImage
}
func previewImage(ctx context.Context, client *http.Client, u *url.URL) string {
	body, _, err := getPreview(ctx, client, u, 160*1024, false)
	if err != nil {
		return ""
	}
	cfg, format, err := image.DecodeConfig(bytes.NewReader(body))
	if err != nil || (format != "png" && format != "jpeg") || cfg.Width > 4096 || cfg.Height > 4096 {
		return ""
	}
	return "data:image/" + format + ";base64," + base64.StdEncoding.EncodeToString(body)
}

var youtubeID = regexp.MustCompile(`^[A-Za-z0-9_-]{11}$`)

func videoID(u *url.URL) string {
	var id string
	switch strings.ToLower(u.Hostname()) {
	case "youtu.be":
		id = strings.Trim(u.Path, "/")
	case "youtube.com", "www.youtube.com", "m.youtube.com":
		if u.Path == "/watch" {
			id = u.Query().Get("v")
		} else {
			p := strings.Split(strings.Trim(u.Path, "/"), "/")
			if len(p) == 2 && (p[0] == "shorts" || p[0] == "embed" || p[0] == "live") {
				id = p[1]
			}
		}
	}
	if youtubeID.MatchString(id) {
		return id
	}
	return ""
}
func fetchCard(ctx context.Context, client *http.Client, u *url.URL) linkCard {
	if id := videoID(u); id != "" {
		q := url.Values{"url": {"https://www.youtube.com/watch?v=" + id}, "format": {"json"}}
		api, _ := url.Parse("https://www.youtube.com/oembed?" + q.Encode())
		b, _, _ := getPreview(ctx, client, api, 32768, false)
		var data struct{ Title string }
		_ = json.Unmarshal(b, &data)
		c := linkCard{Title: shortText(data.Title, 140)}
		img, _ := url.Parse("https://i.ytimg.com/vi/" + id + "/mqdefault.jpg")
		c.Image = previewImage(ctx, client, img)
		return c
	}
	b, final, err := getPreview(ctx, client, u, 768*1024, true)
	if err != nil {
		return linkCard{}
	}
	c, img := parsePreview(b)
	if c.Title != "" && img != "" {
		if u, e := final.Parse(img); e == nil {
			c.Image = previewImage(ctx, client, u)
		}
	}
	return c
}
func (s *workspaceStore) registerLinkPreviews(handle func(string, func(http.ResponseWriter, *http.Request, string))) {
	client := previewHTTP()
	slots := make(chan struct{}, 4)
	cache := map[string]cachedCard{}
	var mu sync.Mutex
	rate := &limiter{clients: map[string]attempt{}}
	handle("GET /api/link-preview", func(w http.ResponseWriter, r *http.Request, actor string) {
		q := r.URL.Query()
		link := q.Get("url")
		address := link
		if strings.HasPrefix(strings.ToLower(address), "www.") {
			address = "https://" + address
		}
		u, err := url.Parse(address)
		if err != nil || !validPreviewURL(u) {
			jsonReply(w, 200, linkCard{})
			return
		}
		s.mu.Lock()
		ch := findChannel(&s.data, q.Get("channel"))
		found := false
		if ch != nil && ch.Kind == "text" {
			for _, m := range s.data.Messages[ch.ID] {
				if m.ID == q.Get("message") && !m.Deleted {
					found = strings.Contains(m.Text, link)
					break
				}
			}
		}
		s.mu.Unlock()
		if !found {
			reject(w, 404, "Ссылка недоступна")
			return
		}
		mu.Lock()
		item, ok := cache[link]
		mu.Unlock()
		if ok && time.Now().Before(item.until) {
			jsonReply(w, 200, item.card)
			return
		}
		if !rate.allowLimit(actor, time.Now(), 60) {
			reject(w, 429, "Слишком много запросов")
			return
		}
		select {
		case slots <- struct{}{}:
			defer func() { <-slots }()
		case <-r.Context().Done():
			return
		case <-time.After(time.Second):
			reject(w, 429, "Попробуйте позже")
			return
		}
		mu.Lock()
		item, ok = cache[link]
		mu.Unlock()
		if ok && time.Now().Before(item.until) {
			jsonReply(w, 200, item.card)
			return
		}
		ctx, cancel := context.WithTimeout(r.Context(), 8*time.Second)
		defer cancel()
		card := fetchCard(ctx, client, u)
		mu.Lock()
		if len(cache) >= 96 {
			for k := range cache {
				delete(cache, k)
				break
			}
		}
		cache[link] = cachedCard{card, time.Now().Add(time.Hour)}
		mu.Unlock()
		jsonReply(w, 200, card)
	})
}
