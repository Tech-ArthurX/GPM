//go:build legacy_searchserver
// +build legacy_searchserver

package main

import (
	"encoding/json"
	"log"
	"net/http"
	"os"
	"strings"
	"time"
)

const pkgFile = "packages.json"
const listenAddr = ":9000"
const reloadInterval = 30 * time.Second

var pkgNames []string
var pkgData []byte
var lastLoaded time.Time

func main() {
	go watchFile()
	http.HandleFunc("/searchapi", searchHandler)
	http.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("ok"))
	})
	log.Println("GPM index server on " + listenAddr)
	log.Fatal(http.ListenAndServe(listenAddr, nil))
}

func watchFile() {
	loadPackages()
	ticker := time.NewTicker(reloadInterval)
	for range ticker.C {
		loadPackages()
	}
}

func loadPackages() {
	fi, err := os.Stat(pkgFile)
	if err != nil || fi.ModTime().Equal(lastLoaded) {
		return
	}
	data, err := os.ReadFile(pkgFile)
	if err != nil {
		return
	}
	var list []string
	if err := json.Unmarshal(data, &list); err != nil {
		return
	}
	pkgNames = list
	pkgData = data
	lastLoaded = fi.ModTime()
	log.Printf("loaded %d packages", len(pkgNames))
}

func searchHandler(w http.ResponseWriter, r *http.Request) {
	q := strings.ToLower(strings.TrimSpace(r.URL.Query().Get("q")))
	w.Header().Set("Content-Type", "application/json")
	if q == "" {
		w.Write(pkgData)
		return
	}
	type item struct {
		Name    string `json:"name"`
		Version string `json:"version"`
		Url     string `json:"url"`
		Size    string `json:"size"`
	}
	var results []map[string]string
	for _, n := range pkgNames {
		if strings.HasPrefix(strings.ToLower(n), q) {
			parts := strings.Split(n, "##")
			it := map[string]string{"name": n}
			if len(parts) > 1 {
				it["version"] = parts[1]
			}
			if len(parts) > 2 {
				it["url"] = parts[2]
			}
			if len(parts) > 3 {
				it["size"] = parts[3]
			}
			results = append(results, it)
			if len(results) >= 50 {
				break
			}
		}
	}
	json.NewEncoder(w).Encode(map[string]any{"results": results, "count": len(results)})
}
