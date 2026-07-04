package main

import (
	"bytes"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

func TestDownloaderRangedProgress(t *testing.T) {
	payload := bytes.Repeat([]byte("gpm-range-data-"), 1024*96)
	var rangeRequests int
	var mu sync.Mutex
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Accept-Ranges", "bytes")
		w.Header().Set("Content-Length", strconv.Itoa(len(payload)))
		if r.Method == http.MethodHead {
			return
		}
		if r.Header.Get("Range") == "" {
			t.Fatalf("expected ranged GET, got no Range header")
		}
		mu.Lock()
		rangeRequests++
		mu.Unlock()
		http.ServeContent(w, r, "pkg.gpm", time.Unix(0, 0), bytes.NewReader(payload))
	}))
	defer server.Close()

	dest := filepath.Join(t.TempDir(), "pkg.gpm")
	var updates []DownloadProgressUpdate
	d := NewDownloader(server.URL, dest, 4, "test")
	d.OnProgress = func(update DownloadProgressUpdate) {
		updates = append(updates, update)
	}
	if err := d.Start(); err != nil {
		t.Fatalf("download: %v", err)
	}
	got, err := os.ReadFile(dest)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, payload) {
		t.Fatalf("downloaded payload mismatch")
	}
	if _, err := os.Stat(dest + ".part"); !os.IsNotExist(err) {
		t.Fatalf("temporary .part file remains: %v", err)
	}
	if rangeRequests < 2 {
		t.Fatalf("range requests = %d, want at least 2", rangeRequests)
	}
	if len(updates) == 0 || updates[len(updates)-1].Percent != 100 || !updates[len(updates)-1].Done {
		t.Fatalf("last progress update = %+v, want done 100%%", updates)
	}
}

func TestDownloaderSingleFallbackProgress(t *testing.T) {
	payload := []byte(strings.Repeat("single-fallback", 2048))
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method == http.MethodHead {
			w.Header().Set("Content-Length", strconv.Itoa(len(payload)))
			return
		}
		_, _ = w.Write(payload)
	}))
	defer server.Close()

	dest := filepath.Join(t.TempDir(), "pkg.gpm")
	var last DownloadProgressUpdate
	d := NewDownloader(server.URL, dest, 8, "test")
	d.OnProgress = func(update DownloadProgressUpdate) {
		last = update
	}
	if err := d.Start(); err != nil {
		t.Fatalf("download: %v", err)
	}
	got, err := os.ReadFile(dest)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, payload) {
		t.Fatalf("downloaded payload mismatch")
	}
	if last.Percent != 100 || !last.Done || last.Threads != 1 {
		t.Fatalf("last progress update = %+v, want single-thread done 100%%", last)
	}
}

func TestDownloaderProbesRangeWhenHeadDoesNotAdvertiseIt(t *testing.T) {
	payload := bytes.Repeat([]byte("range-probe-data-"), 1024*96)
	var rangedRequests int
	var mu sync.Mutex
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Length", strconv.Itoa(len(payload)))
		if r.Method == http.MethodHead {
			return
		}
		if r.Header.Get("Range") == "" {
			t.Fatalf("expected ranged GET after probe, got no Range header")
		}
		mu.Lock()
		rangedRequests++
		mu.Unlock()
		http.ServeContent(w, r, "pkg.gpm", time.Unix(0, 0), bytes.NewReader(payload))
	}))
	defer server.Close()

	dest := filepath.Join(t.TempDir(), "pkg.gpm")
	d := NewDownloader(server.URL, dest, 8, "test")
	if err := d.Start(); err != nil {
		t.Fatalf("download: %v", err)
	}
	got, err := os.ReadFile(dest)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, payload) {
		t.Fatalf("downloaded payload mismatch")
	}
	if rangedRequests < 2 {
		t.Fatalf("ranged requests = %d, want probe plus at least one segment", rangedRequests)
	}
}

func TestDownloaderUsesRangeProbeWhenHeadFails(t *testing.T) {
	payload := bytes.Repeat([]byte("head-fails-range-data-"), 1024*96)
	var rangedRequests int
	var mu sync.Mutex
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method == http.MethodHead {
			http.Error(w, "head disabled", http.StatusMethodNotAllowed)
			return
		}
		w.Header().Set("Content-Length", strconv.Itoa(len(payload)))
		if r.Header.Get("Range") == "" {
			t.Fatalf("expected ranged GET after failed HEAD, got no Range header")
		}
		mu.Lock()
		rangedRequests++
		mu.Unlock()
		http.ServeContent(w, r, "pkg.gpm", time.Unix(0, 0), bytes.NewReader(payload))
	}))
	defer server.Close()

	dest := filepath.Join(t.TempDir(), "pkg.gpm")
	d := NewDownloader(server.URL, dest, 8, "test")
	if err := d.Start(); err != nil {
		t.Fatalf("download: %v", err)
	}
	got, err := os.ReadFile(dest)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, payload) {
		t.Fatalf("downloaded payload mismatch")
	}
	if rangedRequests < 2 {
		t.Fatalf("ranged requests = %d, want metadata probe plus at least one segment", rangedRequests)
	}
}

func TestDownloaderRetriesPartialChunkFromWrittenOffset(t *testing.T) {
	payload := bytes.Repeat([]byte("retry-range-data-"), 1024*96)
	var failedOnce atomic.Bool
	var mu sync.Mutex
	var ranges []string

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Accept-Ranges", "bytes")
		w.Header().Set("Content-Length", strconv.Itoa(len(payload)))
		if r.Method == http.MethodHead {
			return
		}
		rangeHeader := r.Header.Get("Range")
		if rangeHeader == "" {
			t.Fatalf("expected ranged GET, got no Range header")
		}
		mu.Lock()
		ranges = append(ranges, rangeHeader)
		mu.Unlock()

		if strings.HasPrefix(rangeHeader, "bytes=0-") && failedOnce.CompareAndSwap(false, true) {
			w.Header().Set("Content-Range", fmt.Sprintf("bytes 0-%d/%d", len(payload)-1, len(payload)))
			w.WriteHeader(http.StatusPartialContent)
			_, _ = w.Write(payload[:64*1024])
			if flusher, ok := w.(http.Flusher); ok {
				flusher.Flush()
			}
			return
		}
		http.ServeContent(w, r, "pkg.gpm", time.Unix(0, 0), bytes.NewReader(payload))
	}))
	defer server.Close()

	dest := filepath.Join(t.TempDir(), "pkg.gpm")
	d := NewDownloader(server.URL, dest, 4, "test")
	if err := d.Start(); err != nil {
		t.Fatalf("download: %v", err)
	}
	got, err := os.ReadFile(dest)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, payload) {
		t.Fatalf("downloaded payload mismatch")
	}

	foundResume := false
	mu.Lock()
	for _, r := range ranges {
		if strings.HasPrefix(r, "bytes=65536-") {
			foundResume = true
			break
		}
	}
	mu.Unlock()
	if !foundResume {
		t.Fatalf("did not see retry from written offset; ranges=%v", ranges)
	}
}
