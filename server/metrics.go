package main

import (
	"fmt"
	"io"
	"net"
	"net/http"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"
)

// Labels are a fixed vocabulary: never record paths, IPs, names or credentials.
var requestBounds = [...]float64{.005, .01, .025, .05, .1, .25, .5, 1, 2.5, 5, 10}

type requestSeries struct {
	count, bytes uint64
	seconds      float64
	buckets      [len(requestBounds)]uint64
}

type httpMetrics struct {
	mu           sync.Mutex
	started      time.Time
	inFlight     int
	serverErrors uint64
	requests     map[string]*requestSeries
}

func newHTTPMetrics() *httpMetrics {
	m := &httpMetrics{started: time.Now(), requests: make(map[string]*requestSeries)}
	// Establish a zero baseline before the first token/error, so rate/increase
	// can include it when Prometheus has scraped the idle service first.
	for _, status := range []int{200, 400, 401, 403, 415, 429, 500} {
		labels := fmt.Sprintf("route=%q,method=%q,status=%q", "/api/join", "POST", strconv.Itoa(status))
		m.requests[labels] = &requestSeries{}
	}
	return m
}

type measuredResponse struct {
	http.ResponseWriter
	status int
	bytes  uint64
}

func (w *measuredResponse) Unwrap() http.ResponseWriter { return w.ResponseWriter }
func (w *measuredResponse) WriteHeader(status int) {
	if w.status == 0 && status >= 200 {
		w.status = status
	}
	w.ResponseWriter.WriteHeader(status)
}
func (w *measuredResponse) Write(data []byte) (int, error) {
	if w.status == 0 {
		w.WriteHeader(http.StatusOK)
	}
	n, err := w.ResponseWriter.Write(data)
	w.bytes += uint64(n)
	return n, err
}

func metricRoute(path string) string {
	switch path {
	case "/api/join", "/api/config", "/healthz":
		return path
	case "/", "/index.html":
		return "web"
	}
	if strings.HasPrefix(path, "/assets/") {
		return "assets"
	}
	return "other"
}

func (m *httpMetrics) wrap(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		m.mu.Lock()
		m.inFlight++
		m.mu.Unlock()
		response := &measuredResponse{ResponseWriter: w}
		completed := false
		defer func() {
			status := response.status
			if !completed {
				status = 500 // net/http still handles the original panic/connection abort.
			} else if status == 0 {
				status = 200
			}
			method := r.Method
			switch method {
			case "GET", "HEAD", "POST", "OPTIONS":
			default:
				method = "OTHER"
			}
			labels := fmt.Sprintf("route=%q,method=%q,status=%q", metricRoute(r.URL.Path), method, strconv.Itoa(status))
			elapsed := time.Since(start).Seconds()
			m.mu.Lock()
			defer m.mu.Unlock()
			m.inFlight--
			if status >= 500 {
				m.serverErrors++
			}
			series := m.requests[labels]
			if series == nil {
				series = &requestSeries{}
				m.requests[labels] = series
			}
			series.count++
			series.bytes += response.bytes
			series.seconds += elapsed
			for i, bound := range requestBounds {
				if elapsed <= bound {
					series.buckets[i]++
				}
			}
		}()
		next.ServeHTTP(response, r)
		completed = true
	})
}

func (m *httpMetrics) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/metrics" {
		http.NotFound(w, r)
		return
	}
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", "GET")
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	w.Header().Set("Content-Type", "text/plain; version=0.0.4; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	var mem runtime.MemStats
	runtime.ReadMemStats(&mem)
	// Copy under lock; slow scrapers must not stall application requests.
	m.mu.Lock()
	inFlight := m.inFlight
	serverErrors := m.serverErrors
	series := make(map[string]requestSeries, len(m.requests))
	for key, value := range m.requests {
		series[key] = *value
	}
	m.mu.Unlock()
	var b strings.Builder
	fmt.Fprintf(&b, "# TYPE nimbo_process_start_time_seconds gauge\nnimbo_process_start_time_seconds %d\n", m.started.Unix())
	fmt.Fprintf(&b, "# TYPE nimbo_http_in_flight gauge\nnimbo_http_in_flight %d\n", inFlight)
	fmt.Fprintf(&b, "# TYPE nimbo_http_server_errors_total counter\nnimbo_http_server_errors_total %d\n", serverErrors)
	fmt.Fprintf(&b, "# TYPE nimbo_go_goroutines gauge\nnimbo_go_goroutines %d\n", runtime.NumGoroutine())
	fmt.Fprintf(&b, "# TYPE nimbo_go_heap_bytes gauge\nnimbo_go_heap_bytes %d\n", mem.HeapAlloc)
	fmt.Fprintf(&b, "# TYPE nimbo_go_gc_pause_seconds_total counter\nnimbo_go_gc_pause_seconds_total %g\n", float64(mem.PauseTotalNs)/1e9)
	b.WriteString("# TYPE nimbo_http_requests_total counter\n")
	for labels, s := range series {
		fmt.Fprintf(&b, "nimbo_http_requests_total{%s} %d\n", labels, s.count)
	}
	b.WriteString("# TYPE nimbo_http_response_bytes_total counter\n")
	for labels, s := range series {
		fmt.Fprintf(&b, "nimbo_http_response_bytes_total{%s} %d\n", labels, s.bytes)
	}
	b.WriteString("# TYPE nimbo_http_request_duration_seconds histogram\n")
	for labels, s := range series {
		for i, bound := range requestBounds {
			fmt.Fprintf(&b, "nimbo_http_request_duration_seconds_bucket{%s,le=%q} %d\n", labels, strconv.FormatFloat(bound, 'g', -1, 64), s.buckets[i])
		}
		fmt.Fprintf(&b, "nimbo_http_request_duration_seconds_bucket{%s,le=\"+Inf\"} %d\n", labels, s.count)
		fmt.Fprintf(&b, "nimbo_http_request_duration_seconds_sum{%s} %g\n", labels, s.seconds)
		fmt.Fprintf(&b, "nimbo_http_request_duration_seconds_count{%s} %d\n", labels, s.count)
	}
	_, _ = io.WriteString(w, b.String())
}

// The listener is separate from the public API, including reverse-proxy traffic.
func metricsListener(address string) (net.Listener, error) {
	host, port, err := net.SplitHostPort(address)
	if err != nil {
		return nil, fmt.Errorf("NIMBO_METRICS_LISTEN must be a loopback IP and port: %w", err)
	}
	ip := net.ParseIP(host)
	if ip == nil || !ip.IsLoopback() || port == "" {
		return nil, fmt.Errorf("NIMBO_METRICS_LISTEN must use a literal loopback IP")
	}
	return net.Listen("tcp", address)
}
