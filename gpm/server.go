package main

import (
	"context"
	"encoding/json"
	"errors"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

var upgrader = websocket.Upgrader{
	CheckOrigin: checkWebSocketOrigin,
}

type GUIServer struct {
	clients          map[*websocket.Conn]bool
	mu               sync.Mutex
	writeMu          sync.Mutex
	downloads        map[string]*DownloadProgress
	tasks            map[string]*guiTask
	tempResDir       string
	logHistory       []LogEntryGUI
	logMu            sync.Mutex
	debugMode        bool
	startupIndexOnce sync.Once
}

type LogEntryGUI struct {
	Level   string `json:"level"`
	Message string `json:"message"`
	Time    string `json:"time"`
}

type DownloadProgress struct {
	ID         string `json:"id"`
	Package    string `json:"package"`
	Status     string `json:"status"`
	Stage      string `json:"stage"`
	Percent    int    `json:"percent"`
	Downloaded int64  `json:"downloaded,omitempty"`
	Total      int64  `json:"total,omitempty"`
	Speed      int64  `json:"speed,omitempty"`
	Threads    int    `json:"threads,omitempty"`
	Error      string `json:"error,omitempty"`
}

type guiTask struct {
	id     string
	token  string
	cancel context.CancelFunc
}

type WSRequest struct {
	Command string          `json:"command"`
	Params  json.RawMessage `json:"params"`
}

type WSResponse struct {
	Type     string            `json:"type"`
	Data     interface{}       `json:"data,omitempty"`
	Log      *LogEntryGUI      `json:"log,omitempty"`
	Progress *DownloadProgress `json:"progress,omitempty"`
	Dialog   *DialogInfo       `json:"dialog,omitempty"`
}

type DialogInfo struct {
	Type    string   `json:"type"`
	Title   string   `json:"title"`
	Message string   `json:"message"`
	Options []string `json:"options"`
	ID      string   `json:"id"`
}

var guiServer *GUIServer

func startGUIServer(port string, debug bool, noGUISpawn bool, guiClientOverride string) int {
	guiServer = &GUIServer{
		clients:    make(map[*websocket.Conn]bool),
		downloads:  make(map[string]*DownloadProgress),
		tasks:      make(map[string]*guiTask),
		logHistory: make([]LogEntryGUI, 0),
		debugMode:  debug,
	}

	tempDir, err := extractResources()
	if err != nil {
		log.Printf("Failed to extract resources: %v", err)
	} else {
		guiServer.tempResDir = tempDir
	}

	http.HandleFunc("/ws", guiServer.handleWebSocket)
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Write([]byte(`<!doctype html>
<html><head><meta charset="utf-8"><title>GPM Backend</title></head>
<body style="font-family:Segoe UI,Arial,sans-serif;max-width:640px;margin:48px auto;padding:0 16px;line-height:1.5">
<h2>GPM backend is running</h2>
<p>WebSocket endpoint: <code>ws://` + defaultListenHost() + `:` + port + `/ws</code></p>
<p>The recommended UI is <code>gpm-fluent-new.exe</code> shipped next to this binary.
Launch it directly, or pass <code>--ws-port ` + port + `</code> if you started gpm on a different port.</p>
<p>Pass <code>--no-gui-spawn</code> to gpm to suppress auto-launch on this host.</p>
</body></html>`))
	})

	addr := net.JoinHostPort(defaultListenHost(), port)
	log.Printf("GPM Backend started on http://%s", addr)

	// Spawn the GUI client. The backend is the parent process; the
	// client is a regular C++ D2D binary that just talks to /ws.
	if !noGUISpawn {
		spawnGUIClient(port, guiClientOverride)
	}
	guiServer.refreshIndexOnStartup()

	log.Fatal(http.ListenAndServe(addr, nil))
	return 0
}

func checkWebSocketOrigin(r *http.Request) bool {
	origin := strings.TrimSpace(r.Header.Get("Origin"))
	if origin == "" {
		return true
	}

	originURL, err := url.Parse(origin)
	if err != nil {
		return false
	}

	originHost := originURL.Hostname()
	if !isLoopbackAuthority(originHost) {
		return false
	}

	requestHost, requestPort := splitAuthorityHostPort(r.Host)
	if requestHost != "" && !isLoopbackAuthority(requestHost) {
		return false
	}

	if originPort := originURL.Port(); originPort != "" && requestPort != "" && originPort != requestPort {
		return false
	}

	return true
}

func splitAuthorityHostPort(authority string) (string, string) {
	authority = strings.TrimSpace(authority)
	if authority == "" {
		return "", ""
	}
	if host, port, err := net.SplitHostPort(authority); err == nil {
		return strings.Trim(host, "[]"), port
	}
	return strings.Trim(authority, "[]"), ""
}

func isLoopbackAuthority(host string) bool {
	host = strings.TrimSpace(strings.Trim(host, "[]"))
	if host == "" {
		return false
	}
	if strings.EqualFold(host, "localhost") {
		return true
	}
	if ip := net.ParseIP(host); ip != nil {
		return ip.IsLoopback()
	}
	return false
}

func isLoopbackRemoteAddr(remoteAddr string) bool {
	remoteAddr = strings.TrimSpace(remoteAddr)
	if remoteAddr == "" {
		return true
	}
	host, _, err := net.SplitHostPort(remoteAddr)
	if err != nil {
		host = remoteAddr
	}
	return isLoopbackAuthority(host)
}

func defaultListenHost() string {
	if h := strings.TrimSpace(os.Getenv("GPM_LISTEN_HOST")); h != "" {
		return h
	}
	return "127.0.0.1"
}

// spawnGUIClient looks for gpm-fluent-new.exe (or the override) next
// to this binary, or in PATH, and starts it with --ws-port and
// --ws-host. Failures are logged but never fatal — the backend is
// usable headless.
func spawnGUIClient(port string, override string) {
	exePath, err := os.Executable()
	if err != nil {
		log.Printf("GUI spawn: cannot determine self path: %v", err)
		return
	}
	exeDir := filepath.Dir(exePath)

	host := defaultListenHost()
	candidates := []string{}
	if override != "" {
		candidates = append(candidates, override)
	}
	candidates = append(candidates,
		filepath.Join(exeDir, "gpm-fluent-new.exe"),
		filepath.Join(exeDir, "gpm-fluent.exe"),
		"gpm-fluent-new.exe",
		"gpm-fluent.exe",
	)

	for _, c := range candidates {
		if c == "" {
			continue
		}
		// Allow bare names to be resolved by PATH.
		if !strings.ContainsAny(c, `\/`) {
			if _, err := exec.LookPath(c); err != nil {
				continue
			}
		} else if _, err := os.Stat(c); err != nil {
			continue
		}
		args := []string{
			"--ws-host", host,
			"--ws-port", port,
		}
		cmd := exec.Command(c, args...)
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		if err := cmd.Start(); err != nil {
			log.Printf("GUI spawn: failed to start %s: %v", c, err)
			continue
		}
		log.Printf("GUI spawn: started %s (pid %d) with %v", c, cmd.Process.Pid, args)
		go func(c string, pid int) {
			if err := cmd.Wait(); err != nil {
				log.Printf("GUI client %s (pid %d) exited: %v", c, pid, err)
			}
		}(c, cmd.Process.Pid)
		return
	}
	log.Printf("GUI spawn: no gpm-fluent client found (looked next to %s and in PATH); pass --no-gui-spawn to silence", exePath)
}

func (s *GUIServer) handleWebSocket(w http.ResponseWriter, r *http.Request) {
	if !isLoopbackRemoteAddr(r.RemoteAddr) {
		http.Error(w, "forbidden", http.StatusForbidden)
		return
	}

	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	defer conn.Close()

	s.mu.Lock()
	s.clients[conn] = true
	s.mu.Unlock()

	defer func() {
		s.mu.Lock()
		delete(s.clients, conn)
		s.mu.Unlock()
	}()

	s.sendConfig(conn)
	s.sendInstalledList(conn)
	s.sendIndexData(conn)

	for {
		_, msg, err := conn.ReadMessage()
		if err != nil {
			break
		}

		var req WSRequest
		if err := json.Unmarshal(msg, &req); err != nil {
			continue
		}

		s.handleMessage(conn, req)
	}
}

func (s *GUIServer) handleMessage(conn *websocket.Conn, req WSRequest) {
	switch req.Command {
	case "install":
		var params struct {
			Packages []string             `json:"packages"`
			Version  string               `json:"version"`
			Items    []installRequestItem `json:"items"`
		}
		if err := json.Unmarshal(req.Params, &params); err == nil {
			if len(params.Items) > 0 {
				go s.installPackageItems(conn, params.Items)
			} else {
				go s.installPackages(conn, params.Packages, params.Version)
			}
		}
	case "uninstall":
		var params struct {
			Packages []string `json:"packages"`
		}
		if err := json.Unmarshal(req.Params, &params); err == nil {
			go s.uninstallPackages(conn, params.Packages)
		}
	case "update_index":
		go s.updateIndex(conn)
	case "cancel_task":
		var params struct {
			ID string `json:"id"`
		}
		if err := json.Unmarshal(req.Params, &params); err == nil {
			s.cancelTask(params.ID)
		}
	case "dialog_response":
		var params struct {
			ID       string `json:"id"`
			Response string `json:"response"`
		}
		if err := json.Unmarshal(req.Params, &params); err == nil {
			s.handleDialogResponse(conn, params.ID, params.Response)
		}
	case "get_installed":
		s.sendInstalledList(conn)
	case "get_index":
		s.sendIndexData(conn)
	case "search":
		var params struct {
			Query string `json:"query"`
		}
		if err := json.Unmarshal(req.Params, &params); err == nil {
			s.searchPackages(conn, params.Query)
		}
	}
}

func (s *GUIServer) registerTask(id string) (context.Context, func()) {
	ctx, cancel := context.WithCancel(context.Background())
	token := time.Now().Format(time.RFC3339Nano)
	s.mu.Lock()
	if old := s.tasks[id]; old != nil {
		old.cancel()
	}
	s.tasks[id] = &guiTask{id: id, token: token, cancel: cancel}
	s.mu.Unlock()
	cleanup := func() {
		s.mu.Lock()
		if current := s.tasks[id]; current != nil && current.token == token {
			delete(s.tasks, id)
		}
		s.mu.Unlock()
	}
	return ctx, cleanup
}

func (s *GUIServer) cancelTask(id string) {
	id = strings.TrimSpace(id)
	if id == "" {
		id = "active"
	}
	s.mu.Lock()
	var cancelled []string
	if id == "active" || id == "*" || id == "all" {
		for taskID, task := range s.tasks {
			task.cancel()
			cancelled = append(cancelled, taskID)
		}
	} else if task := s.tasks[id]; task != nil {
		task.cancel()
		cancelled = append(cancelled, id)
	}
	s.mu.Unlock()
	if len(cancelled) == 0 {
		s.logGUI("INFO", "No active task to cancel")
		return
	}
	sort.Strings(cancelled)
	s.logGUI("WARN", "Cancellation requested: "+strings.Join(cancelled, ", "))
	s.broadcast(WSResponse{Type: "progress", Progress: &DownloadProgress{
		ID:      cancelled[0],
		Status:  "Cancelling...",
		Stage:   "cancel",
		Percent: 0,
	}})
}

func (s *GUIServer) send(conn *websocket.Conn, resp WSResponse) {
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	_ = conn.WriteJSON(resp)
}

func (s *GUIServer) broadcast(resp WSResponse) {
	s.mu.Lock()
	clients := make([]*websocket.Conn, 0, len(s.clients))
	for conn := range s.clients {
		clients = append(clients, conn)
	}
	s.mu.Unlock()
	for _, conn := range clients {
		s.send(conn, resp)
	}
}

func (s *GUIServer) logGUI(level, message string) {
	entry := LogEntryGUI{
		Level:   level,
		Message: message,
		Time:    time.Now().Format("15:04:05"),
	}

	s.logMu.Lock()
	s.logHistory = append(s.logHistory, entry)
	if len(s.logHistory) > 100 {
		s.logHistory = s.logHistory[1:]
	}
	s.logMu.Unlock()

	LogDebug("[%s] %s", level, message)
	s.broadcast(WSResponse{Type: "log", Log: &entry})
}

func (s *GUIServer) sendProgress(conn *websocket.Conn, progress *DownloadProgress) {
	s.send(conn, WSResponse{Type: "progress", Progress: progress})
}

func (s *GUIServer) sendConfig(conn *websocket.Conn) {
	s.send(conn, WSResponse{
		Type: "config",
		Data: map[string]interface{}{
			"debug": s.debugMode,
		},
	})
}

func (s *GUIServer) sendInstalledList(conn *websocket.Conn) {
	pm, err := NewPackageManager()
	if err != nil {
		s.send(conn, WSResponse{Type: "installed_data", Data: []InstalledPackage{}})
		return
	}

	var list []InstalledPackage
	for _, pkg := range pm.Packages {
		list = append(list, pkg)
	}
	sort.Slice(list, func(i, j int) bool {
		if list[i].Name == list[j].Name {
			return CompareVersions(list[i].Version, list[j].Version) > 0
		}
		return list[i].Name < list[j].Name
	})
	s.send(conn, WSResponse{Type: "installed_data", Data: list})
}

func (s *GUIServer) sendIndexData(conn *websocket.Conn) {
	index, err := loadLocalIndex()
	if err != nil {
		s.send(conn, WSResponse{Type: "index_data", Data: []PackageIndexItem{}})
		return
	}
	s.send(conn, WSResponse{Type: "index_data", Data: index})
}

func (s *GUIServer) broadcastIndexData() {
	index, err := loadLocalIndex()
	if err != nil {
		s.broadcast(WSResponse{Type: "index_data", Data: []PackageIndexItem{}})
		return
	}
	s.broadcast(WSResponse{Type: "index_data", Data: index})
}

func (s *GUIServer) refreshIndexOnStartup() {
	s.startupIndexOnce.Do(func() {
		go func() {
			ctx, cancel := context.WithTimeout(context.Background(), 45*time.Second)
			defer cancel()
			s.logGUI("INFO", "Refreshing package index on startup...")
			if err := updateLocalIndexContext(ctx); err != nil {
				s.logGUI("WARN", "Startup index refresh skipped: "+err.Error())
				return
			}
			s.logGUI("SUCCESS", "Startup index refresh complete")
			s.broadcastIndexData()
		}()
	})
}

func (s *GUIServer) updateIndex(conn *websocket.Conn) {
	ctx, cleanup := s.registerTask("active")
	defer cleanup()
	s.logGUI("INFO", "Updating package index...")
	s.sendProgress(conn, &DownloadProgress{
		ID:      "index",
		Status:  "Connecting to server...",
		Stage:   "update",
		Percent: 10,
	})

	if err := updateLocalIndexContext(ctx); err != nil {
		if errors.Is(err, context.Canceled) {
			s.sendProgress(conn, &DownloadProgress{
				ID:      "index",
				Status:  "Cancelled",
				Stage:   "cancel",
				Percent: 0,
				Error:   err.Error(),
			})
			return
		}
		s.logGUI("ERROR", "Update failed: "+err.Error())
		s.sendProgress(conn, &DownloadProgress{
			ID:     "index",
			Status: "Failed",
			Stage:  "error",
			Error:  err.Error(),
		})
		return
	}

	s.sendProgress(conn, &DownloadProgress{
		ID:      "index",
		Status:  "Done",
		Stage:   "update",
		Percent: 100,
	})

	s.logGUI("SUCCESS", "Index updated successfully")
	s.broadcastIndexData()
}

func (s *GUIServer) searchPackages(conn *websocket.Conn, query string) {
	index, err := loadLocalIndex()
	if err != nil {
		s.send(conn, WSResponse{Type: "search_result", Data: []PackageIndexItem{}})
		return
	}

	needle := strings.ToLower(query)
	var results []PackageIndexItem
	for _, pkg := range index {
		if strings.Contains(strings.ToLower(pkg.Name), needle) ||
			strings.Contains(strings.ToLower(pkg.Author), needle) ||
			strings.Contains(strings.ToLower(pkg.Category), needle) ||
			strings.Contains(strings.ToLower(pkg.Version), needle) {
			results = append(results, pkg)
		}
	}

	s.send(conn, WSResponse{Type: "search_result", Data: results})
}

func (s *GUIServer) getDownloadDir() string {
	exePath, _ := os.Executable()
	return filepath.Join(filepath.Dir(exePath), "downloads")
}
