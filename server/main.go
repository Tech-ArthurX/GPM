package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

const defaultPort = "8787"
const defaultPackagesFile = "packages.json"

type packageEntry struct {
	Name        string `json:"name"`
	Version     string `json:"version"`
	Author      string `json:"author"`
	Category    string `json:"category,omitempty"`
	Description string `json:"description,omitempty"`
	Size        int64  `json:"size"`
	SHA256      string `json:"sha256,omitempty"`
	Filename    string `json:"filename,omitempty"`
	URL         string `json:"url"`
	Yanked      bool   `json:"yanked,omitempty"`
}

type registryCache struct {
	mu       sync.RWMutex
	path     string
	modTime  time.Time
	data     []byte
	etag     string
	count    int
	lastHash string
}

func main() {
	packagesFile := strings.TrimSpace(os.Getenv("PACKAGES_FILE"))
	if packagesFile == "" {
		packagesFile = defaultPackagesFile
	}

	cache := &registryCache{path: packagesFile}
	if err := cache.reload(); err != nil {
		log.Fatalf("load registry failed: %v", err)
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/", cache.handle)

	addr := net.JoinHostPort(listenHost(), listenPort())
	log.Printf("GPM registry server listening on http://%s", addr)
	log.Printf("registry: %s (%d packages, sha256=%s)", cache.path, cache.count, cache.lastHash)
	log.Fatal(http.ListenAndServe(addr, mux))
}

func listenHost() string {
	if host := strings.TrimSpace(os.Getenv("HOST")); host != "" {
		return host
	}
	if host := strings.TrimSpace(os.Getenv("LISTEN_HOST")); host != "" {
		return host
	}
	return "127.0.0.1"
}

func listenPort() string {
	if port := strings.TrimSpace(os.Getenv("PORT")); port != "" {
		return port
	}
	return defaultPort
}

func (c *registryCache) handle(w http.ResponseWriter, r *http.Request) {
	path := cleanRequestPath(r.URL.Path)
	switch path {
	case "/", "/health", "/healthz":
		c.handleHealth(w, r)
	case "/index", "/packages.json", "/registry.json", "/gpm/index", "/gpm/packages.json", "/gpm/registry.json":
		c.handleRegistry(w, r)
	default:
		http.NotFound(w, r)
	}
}

func cleanRequestPath(path string) string {
	if path == "" {
		return "/"
	}
	path = "/" + strings.TrimLeft(path, "/")
	return filepath.ToSlash(filepath.Clean(path))
}

func (c *registryCache) handleHealth(w http.ResponseWriter, r *http.Request) {
	if err := c.reloadIfChanged(); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	c.mu.RLock()
	defer c.mu.RUnlock()
	writeJSON(w, map[string]any{
		"ok":       true,
		"packages": c.count,
		"sha256":   c.lastHash,
	})
}

func (c *registryCache) handleRegistry(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		w.Header().Set("Allow", "GET, HEAD")
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if err := c.reloadIfChanged(); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	c.mu.RLock()
	data := append([]byte(nil), c.data...)
	etag := c.etag
	c.mu.RUnlock()

	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Cache-Control", "public, max-age=60")
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("ETag", etag)
	if r.Header.Get("If-None-Match") == etag {
		w.WriteHeader(http.StatusNotModified)
		return
	}
	if r.Method == http.MethodHead {
		return
	}
	_, _ = w.Write(data)
}

func (c *registryCache) reloadIfChanged() error {
	stat, err := os.Stat(c.path)
	if err != nil {
		return err
	}

	c.mu.RLock()
	same := !stat.ModTime().After(c.modTime)
	c.mu.RUnlock()
	if same {
		return nil
	}
	return c.reload()
}

func (c *registryCache) reload() error {
	data, err := os.ReadFile(c.path)
	if err != nil {
		return err
	}
	var entries []packageEntry
	if err := json.Unmarshal(data, &entries); err != nil {
		return fmt.Errorf("invalid packages json: %w", err)
	}
	for i, entry := range entries {
		if strings.TrimSpace(entry.Name) == "" || strings.TrimSpace(entry.Version) == "" ||
			strings.TrimSpace(entry.Author) == "" || strings.TrimSpace(entry.URL) == "" {
			return fmt.Errorf("invalid package entry at index %d", i)
		}
	}
	stat, err := os.Stat(c.path)
	if err != nil {
		return err
	}
	sum := sha256.Sum256(data)
	hash := hex.EncodeToString(sum[:])

	c.mu.Lock()
	c.data = data
	c.modTime = stat.ModTime()
	c.etag = `"` + hash + `"`
	c.count = len(entries)
	c.lastHash = hash
	c.mu.Unlock()
	return nil
}

func writeJSON(w http.ResponseWriter, payload any) {
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	_ = json.NewEncoder(w).Encode(payload)
}
