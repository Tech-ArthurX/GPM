package main

import (
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
)

func TestCompareVersionsNumeric(t *testing.T) {
	cases := []struct {
		a, b string
		want int
	}{
		{"1.0", "1.0", 0},
		{"1.0", "1.0.1", -1},
		{"1.0.1", "1.0", 1},
		{"1.10", "1.9", 1},
		{"1.9", "1.10", -1},
		{"v2.0", "2.0", 0},
		{"2.0", "v1.99", 1},
		{"1.0-beta", "1.0.1", -1},
		{"1.0", "1.0-beta", 0},
		{"", "1.0", -1},
		{"1.0", "", 1},
		{"", "", 0},
		{"1.0.0", "1", 0},
		{"1.10.0", "1.9.99", 1},
		{"1.0a1", "1.0a2", -1},
	}
	for _, tc := range cases {
		got := compareVersionsNumeric(tc.a, tc.b)
		if got != tc.want {
			t.Errorf("compareVersionsNumeric(%q, %q) = %d, want %d", tc.a, tc.b, got, tc.want)
		}
	}
}

func TestSplitVersionDigits(t *testing.T) {
	cases := []struct {
		in   string
		want []int
	}{
		{"1.0", []int{1, 0}},
		{"1.10.0", []int{1, 10, 0}},
		{"v1.2.3", []int{1, 2, 3}},
		{"1.0-beta", []int{1, 0}},
		{"", nil},
		{"abc", nil},
		{"1.0.1-rc2", []int{1, 0, 1, 2}},
	}
	for _, tc := range cases {
		got := splitVersionDigits(tc.in)
		if !equalIntSlice(got, tc.want) {
			t.Errorf("splitVersionDigits(%q) = %v, want %v", tc.in, got, tc.want)
		}
	}
}

func equalIntSlice(a, b []int) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func TestRegistryAllowsPublicIndex(t *testing.T) {
	cache := newTestRegistryCache(t, validPackagesJSON())
	req := httptest.NewRequest(http.MethodGet, "http://example.test/index", nil)
	req.Header.Set("User-Agent", "curl/8.0")

	rec := httptest.NewRecorder()
	cache.handle(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want %d; body=%q", rec.Code, http.StatusOK, rec.Body.String())
	}
	if contentType := rec.Header().Get("Content-Type"); contentType != "application/json; charset=utf-8" {
		t.Fatalf("Content-Type = %q, want application/json", contentType)
	}
}

func TestRegistryHeadReturnsHeadersOnly(t *testing.T) {
	cache := newTestRegistryCache(t, validPackagesJSON())
	req := httptest.NewRequest(http.MethodHead, "http://example.test/packages.json", nil)

	rec := httptest.NewRecorder()
	cache.handle(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want %d", rec.Code, http.StatusOK)
	}
	if rec.Body.Len() != 0 {
		t.Fatalf("HEAD body length = %d, want 0", rec.Body.Len())
	}
	if rec.Header().Get("ETag") == "" {
		t.Fatalf("missing ETag")
	}
}

func TestHealthDoesNotRequireAuth(t *testing.T) {
	cache := newTestRegistryCache(t, validPackagesJSON())
	req := httptest.NewRequest(http.MethodGet, "http://example.test/healthz", nil)

	rec := httptest.NewRecorder()
	cache.handle(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want %d; body=%q", rec.Code, http.StatusOK, rec.Body.String())
	}
}

func TestRegistryRejectsInvalidEntry(t *testing.T) {
	dir := t.TempDir()
	packagesPath := filepath.Join(dir, "packages.json")
	if err := os.WriteFile(packagesPath, []byte(`[{"name":"Broken","version":"1.0","author":"ArthurX"}]`), 0o600); err != nil {
		t.Fatal(err)
	}
	cache := &registryCache{path: packagesPath}
	if err := cache.reload(); err == nil {
		t.Fatalf("reload succeeded, want invalid entry error")
	}
}

func newTestRegistryCache(t *testing.T, data []byte) *registryCache {
	t.Helper()
	dir := t.TempDir()
	packagesPath := filepath.Join(dir, "packages.json")
	if err := os.WriteFile(packagesPath, data, 0o600); err != nil {
		t.Fatal(err)
	}

	cache := &registryCache{path: packagesPath}
	if err := cache.reload(); err != nil {
		t.Fatal(err)
	}
	return cache
}

func validPackagesJSON() []byte {
	return []byte(`[{"name":"Everything","version":"1.0","author":"ArthurX","category":"Search","size":1,"url":"https://example.test/Everything.gpm"}]`)
}
