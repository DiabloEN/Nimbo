package main

import (
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
)

func scrape(m *httpMetrics) string {
	w := httptest.NewRecorder()
	m.ServeHTTP(w, httptest.NewRequest("GET", "/metrics", nil))
	return w.Body.String()
}

func TestMetricsRequestsAndPrivacy(t *testing.T) {
	m := newHTTPMetrics()
	initialSeries := len(m.requests)
	base := newHandler(testConfig())
	_, session, _ := appLogin(t, base, "Private Name", "")
	h := m.wrap(base)
	join(h, `{"name":"Private Name","password":"wrong-secret","room":"lounge"}`, "")
	join(h, `{"name":"Private Name","room":"lounge"}`, "", session)
	for i := 0; i < 100; i++ {
		w := httptest.NewRecorder()
		h.ServeHTTP(w, httptest.NewRequest("GET", fmt.Sprintf("/unknown/%d?token=private-token", i), nil))
	}
	text := scrape(m)
	for _, want := range []string{
		`nimbo_http_requests_total{route="/api/join",method="POST",status="401"} 1`,
		`nimbo_http_requests_total{route="/api/join",method="POST",status="200"} 1`,
		`nimbo_http_requests_total{route="other",method="GET",status="404"} 100`,
		`nimbo_http_request_duration_seconds_bucket{route="other",method="GET",status="404",le="+Inf"} 100`,
		"nimbo_http_in_flight 0\n",
	} {
		if !strings.Contains(text, want) {
			t.Errorf("missing %s", want)
		}
	}
	for _, secret := range []string{"Private Name", "wrong-secret", "long-test-password", "private-token", "/unknown/", "guest-"} {
		if strings.Contains(text, secret) {
			t.Errorf("private data leaked: %s", secret)
		}
	}
	if len(m.requests) != initialSeries+1 {
		t.Fatal("unbounded request labels", len(m.requests))
	}
	// Scraping does not count itself; the public API never exposes diagnostics.
	before := scrape(m)
	_ = scrape(m)
	if !strings.Contains(before, `status="404"} 100`) {
		t.Fatal("wrong counter")
	}
	w := httptest.NewRecorder()
	h.ServeHTTP(w, httptest.NewRequest("GET", "/metrics", nil))
	if w.Code != 404 {
		t.Fatal("public metrics exposed")
	}
}

func TestMetricsInFlightAndConcurrentScrapes(t *testing.T) {
	m := newHTTPMetrics()
	entered, release := make(chan struct{}), make(chan struct{})
	h := m.wrap(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		close(entered)
		<-release
		_, _ = w.Write([]byte("done"))
	}))
	var wg sync.WaitGroup
	wg.Add(1)
	go func() { defer wg.Done(); h.ServeHTTP(httptest.NewRecorder(), httptest.NewRequest("GET", "/", nil)) }()
	<-entered
	if !strings.Contains(scrape(m), "nimbo_http_in_flight 1\n") {
		t.Error("missing in-flight request")
	}
	close(release)
	wg.Wait()
	if !strings.Contains(scrape(m), `nimbo_http_response_bytes_total{route="web",method="GET",status="200"} 4`) {
		t.Fatal("wrong byte count")
	}
	h = m.wrap(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { w.WriteHeader(204) }))
	for i := 0; i < 30; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			h.ServeHTTP(httptest.NewRecorder(), httptest.NewRequest("GET", "/", nil))
			_ = scrape(m)
		}()
	}
	wg.Wait()
	if !strings.Contains(scrape(m), `nimbo_http_requests_total{route="web",method="GET",status="204"} 30`) {
		t.Fatal("lost concurrent requests")
	}
}

func TestMetricsPanicAndEmptyResponse(t *testing.T) {
	m := newHTTPMetrics()
	func() {
		defer func() {
			if recover() == nil {
				t.Error("panic swallowed")
			}
		}()
		m.wrap(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { panic("failure") })).ServeHTTP(httptest.NewRecorder(), httptest.NewRequest("GET", "/", nil))
	}()
	m.wrap(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {})).ServeHTTP(httptest.NewRecorder(), httptest.NewRequest("GET", "/", nil))
	text := scrape(m)
	if !strings.Contains(text, "nimbo_http_server_errors_total 1\n") {
		t.Error("panic was not counted as a server error")
	}
	if !strings.Contains(text, `status="500"} 1`) || !strings.Contains(text, `status="200"} 1`) || !strings.Contains(text, "nimbo_http_in_flight 0\n") {
		t.Fatal(text)
	}
}

func TestMetricsListenerIsolation(t *testing.T) {
	for _, address := range []string{":9091", "0.0.0.0:9091", "[::]:9091", "192.168.1.2:9091", "localhost:9091", "bad"} {
		l, err := metricsListener(address)
		if err == nil {
			l.Close()
			t.Errorf("accepted unsafe bind %s", address)
		}
	}
	l, err := metricsListener("127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	l.Close()
	m := newHTTPMetrics()
	for _, test := range []struct {
		method, path string
		status       int
	}{{"GET", "/metrics", 200}, {"POST", "/metrics", 405}, {"GET", "/debug/pprof", 404}} {
		w := httptest.NewRecorder()
		m.ServeHTTP(w, httptest.NewRequest(test.method, test.path, nil))
		if w.Code != test.status {
			t.Fatal(test, w.Code)
		}
	}
}
