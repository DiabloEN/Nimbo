package main

import (
	"context"
	"net/http"
	"net/url"
	"strings"
)

type portableClientContext struct{}

func portableClient(r *http.Request) bool { return r.Context().Value(portableClientContext{}) == true }
func allowedAPIOrigin(c config, r *http.Request) bool {
	origin := r.Header.Get("Origin")
	return origin == "" || origin == c.Origin || portableClient(r)
}

// Portable clients authenticate with this server's key/token, never ambient
// browser cookies. Cookie session and streaming routes remain same-origin only.
func portableClients(c config, next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		origin := r.Header.Get("Origin")
		if origin == "" || origin == c.Origin {
			next.ServeHTTP(w, r)
			return
		}
		if !strings.HasPrefix(r.URL.Path, "/api/") {
			next.ServeHTTP(w, r)
			return
		}
		u, err := url.Parse(origin)
		valid := err == nil && u.Host != "" && u.User == nil && u.RawQuery == "" && u.Fragment == "" && u.Path == "" && (u.Scheme == "https" || (u.Scheme == "http" && (u.Hostname() == "localhost" || u.Hostname() == "127.0.0.1" || u.Hostname() == "::1")))
		if !valid || r.URL.Path == "/api/session" || strings.HasPrefix(r.URL.Path, "/api/session/") {
			reject(w, 403, "Недопустимый источник запроса")
			return
		}
		w.Header().Add("Vary", "Origin")
		w.Header().Set("Access-Control-Allow-Origin", origin)
		w.Header().Set("Access-Control-Expose-Headers", "Content-Type, Content-Disposition, Retry-After")
		if r.Method == "OPTIONS" {
			method := r.Header.Get("Access-Control-Request-Method")
			if method != "GET" && method != "POST" && method != "PATCH" && method != "PUT" && method != "DELETE" {
				reject(w, 403, "Метод недоступен")
				return
			}
			marker := false
			for _, h := range strings.Split(r.Header.Get("Access-Control-Request-Headers"), ",") {
				h = strings.ToLower(strings.TrimSpace(h))
				if h == "x-nimbo-client" {
					marker = true
				}
				if h != "content-type" && h != "authorization" && h != "x-nimbo-client" {
					reject(w, 403, "Заголовок недоступен")
					return
				}
			}
			if !marker {
				reject(w, 403, "Нужен клиент Nimbo")
				return
			}
			w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PATCH, PUT, DELETE")
			w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Nimbo-Client")
			w.Header().Set("Access-Control-Max-Age", "600")
			w.WriteHeader(http.StatusNoContent)
			return
		}
		if r.Header.Get("X-Nimbo-Client") != "1" {
			reject(w, 403, "Недопустимый источник запроса")
			return
		}
		r = r.Clone(context.WithValue(r.Context(), portableClientContext{}, true))
		r.Header.Del("Cookie")
		next.ServeHTTP(w, r)
	})
}
