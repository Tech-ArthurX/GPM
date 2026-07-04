package main

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/gorilla/websocket"
)

type installConflictAction string

const (
	installConflictPrompt    installConflictAction = ""
	installConflictReinstall installConflictAction = "reinstall"
	installConflictDowngrade installConflictAction = "downgrade"
)

type installDialogPayload struct {
	Action           string `json:"action"`
	Name             string `json:"name"`
	Version          string `json:"version"`
	InstalledVersion string `json:"installed_version"`
}

type installRequestItem struct {
	Name    string `json:"name"`
	Version string `json:"version,omitempty"`
}

func (s *GUIServer) installPackages(conn *websocket.Conn, names []string, version string) {
	items := make([]installRequestItem, 0, len(names))
	for _, name := range names {
		items = append(items, installRequestItem{Name: name, Version: version})
	}
	s.installPackageItems(conn, items)
}

func (s *GUIServer) installPackageItems(conn *websocket.Conn, items []installRequestItem) {
	ctx, cleanup := s.registerTask("active")
	defer cleanup()
	for _, item := range items {
		name := strings.TrimSpace(item.Name)
		if name == "" {
			continue
		}
		if err := ctx.Err(); err != nil {
			s.sendCancelledProgress(conn, "active", "", err)
			return
		}
		if pendingDialog := s.installSinglePackage(ctx, conn, name, item.Version, installConflictPrompt, ""); pendingDialog {
			s.logGUI("WARN", fmt.Sprintf("Batch install paused at version conflict: %s", name))
			return
		}
	}
	s.sendInstalledList(conn)
}

func (s *GUIServer) installSinglePackage(ctx context.Context, conn *websocket.Conn, name string, version string, confirmed installConflictAction, expectedInstalledVersion string) bool {
	taskID := "dl_" + name
	if err := ctx.Err(); err != nil {
		s.sendCancelledProgress(conn, taskID, name, err)
		return false
	}
	index, err := loadLocalIndex()
	if err != nil {
		s.logGUI("ERROR", fmt.Sprintf("Failed to load index: %v", err))
		return false
	}

	pkg := selectPackageFromIndex(index, name, version)
	if pkg == nil {
		s.logGUI("ERROR", fmt.Sprintf("Package not found: %s", name))
		return false
	}

	pm, _ := NewPackageManager()
	if installed, exists := pm.Get(pkg.Name); exists {
		cmp := CompareVersions(installed.Version, pkg.Version)
		if cmp == 0 {
			if confirmed == installConflictReinstall && installed.Version == expectedInstalledVersion {
				s.logGUI("WARN", fmt.Sprintf("Reinstall confirmed: %s v%s", pkg.Name, pkg.Version))
			} else {
				s.sendInstallConflictDialog(conn, installConflictReinstall, *pkg, installed)
				return true
			}
		}
		if cmp > 0 {
			if confirmed == installConflictDowngrade && installed.Version == expectedInstalledVersion {
				s.logGUI("WARN", fmt.Sprintf("Downgrade confirmed: %s v%s -> v%s", pkg.Name, installed.Version, pkg.Version))
			} else {
				s.sendInstallConflictDialog(conn, installConflictDowngrade, *pkg, installed)
				return true
			}
		}
		if cmp < 0 {
			s.logGUI("INFO", fmt.Sprintf("Upgrade detected: %s v%s -> v%s", pkg.Name, installed.Version, pkg.Version))
		}
	}

	s.logGUI("INFO", fmt.Sprintf("Downloading %s v%s...", pkg.Name, pkg.Version))
	s.sendProgress(conn, &DownloadProgress{
		ID:      taskID,
		Package: pkg.Name,
		Status:  "Downloading...",
		Stage:   "download",
		Percent: 0,
	})

	downloadDir := s.getDownloadDir()
	_ = os.MkdirAll(downloadDir, 0755)
	downloadFileName, err := packageDownloadFileName(pkg.Name)
	if err != nil {
		s.logGUI("ERROR", fmt.Sprintf("Unsafe package name: %v", err))
		s.sendProgress(conn, &DownloadProgress{
			ID:     taskID,
			Status: "Failed",
			Stage:  "error",
			Error:  err.Error(),
		})
		return false
	}
	destPath := filepath.Join(downloadDir, downloadFileName)
	if err := ensurePathWithinRoot(downloadDir, destPath); err != nil {
		s.logGUI("ERROR", fmt.Sprintf("Unsafe download path: %v", err))
		s.sendProgress(conn, &DownloadProgress{
			ID:     taskID,
			Status: "Failed",
			Stage:  "error",
			Error:  err.Error(),
		})
		return false
	}

	downloader := NewDownloader(pkg.URL, destPath, defaultDownloadThreads, "GPM-GUI/1.0")
	downloader.OnProgress = func(update DownloadProgressUpdate) {
		s.sendProgress(conn, &DownloadProgress{
			ID:         taskID,
			Package:    pkg.Name,
			Status:     downloadStatusText(update),
			Stage:      "download",
			Percent:    update.Percent,
			Downloaded: update.Downloaded,
			Total:      update.Total,
			Speed:      update.Speed,
			Threads:    update.Threads,
		})
	}
	if err := downloader.StartContext(ctx); err != nil {
		if errors.Is(err, context.Canceled) {
			_ = os.Remove(destPath)
			_ = os.Remove(destPath + ".part")
			s.sendCancelledProgress(conn, taskID, pkg.Name, err)
			return false
		}
		s.logGUI("ERROR", fmt.Sprintf("Download failed: %v", err))
		s.sendProgress(conn, &DownloadProgress{
			ID:     taskID,
			Status: "Failed",
			Stage:  "error",
			Error:  err.Error(),
		})
		return false
	}
	if err := ctx.Err(); err != nil {
		_ = os.Remove(destPath)
		s.sendCancelledProgress(conn, taskID, pkg.Name, err)
		return false
	}
	downloadedSize := pkg.Size
	if stat, err := os.Stat(destPath); err == nil {
		downloadedSize = stat.Size()
	}
	s.sendProgress(conn, &DownloadProgress{
		ID:         taskID,
		Package:    pkg.Name,
		Status:     "Verifying download...",
		Stage:      "verify",
		Percent:    100,
		Downloaded: downloadedSize,
		Total:      downloadedSize,
	})
	if err := verifyFileSHA256(destPath, pkg.SHA256); err != nil {
		s.logGUI("ERROR", fmt.Sprintf("Integrity check failed: %v", err))
		s.sendProgress(conn, &DownloadProgress{
			ID:     taskID,
			Status: "Failed",
			Stage:  "error",
			Error:  err.Error(),
		})
		return false
	}
	s.sendProgress(conn, &DownloadProgress{
		ID:      taskID,
		Package: pkg.Name,
		Status:  "Installing...",
		Stage:   "install",
		Percent: 100,
	})

	success, err := installContext(ctx, destPath, s.tempResDir, true)
	if err != nil {
		if errors.Is(err, context.Canceled) {
			s.sendCancelledProgress(conn, taskID, pkg.Name, err)
			return false
		}
		s.logGUI("ERROR", fmt.Sprintf("Installation failed: %v", err))
		s.sendProgress(conn, &DownloadProgress{
			ID:      taskID,
			Status:  "Failed: " + err.Error(),
			Stage:   "error",
			Percent: 0,
		})
		return false
	}

	if success {
		s.sendProgress(conn, &DownloadProgress{
			ID:      taskID,
			Package: pkg.Name,
			Status:  "Done",
			Stage:   "done",
			Percent: 100,
		})
		s.logGUI("SUCCESS", fmt.Sprintf("Successfully installed: %s v%s", pkg.Name, pkg.Version))
	}
	_ = os.Remove(destPath)
	return false
}

func downloadStatusText(update DownloadProgressUpdate) string {
	if update.Total > 0 {
		if update.Speed > 0 {
			return fmt.Sprintf("Downloading %d%% at %s/s", update.Percent, formatBytes(update.Speed))
		}
		return fmt.Sprintf("Downloading %d%%", update.Percent)
	}
	return "Downloading..."
}

func (s *GUIServer) sendCancelledProgress(conn *websocket.Conn, taskID string, packageName string, err error) {
	s.logGUI("WARN", "Task cancelled")
	s.sendProgress(conn, &DownloadProgress{
		ID:      taskID,
		Package: packageName,
		Status:  "Cancelled",
		Stage:   "cancel",
		Percent: 0,
		Error:   err.Error(),
	})
}

func (s *GUIServer) uninstallPackages(conn *websocket.Conn, names []string) {
	for _, name := range names {
		if err := uninstall(name, true); err != nil {
			s.logGUI("WARN", fmt.Sprintf("Uninstall skipped: %s (%v)", name, err))
			continue
		}
		s.logGUI("SUCCESS", fmt.Sprintf("Uninstalled: %s", name))
	}
	s.sendInstalledList(conn)
}

func (s *GUIServer) handleDialogResponse(conn *websocket.Conn, id, response string) {
	payload, ok := parseInstallDialogID(id)
	if !ok {
		s.logGUI("WARN", "Ignored unknown dialog response: "+id)
		return
	}

	action := installConflictAction(payload.Action)
	accepted := false
	switch action {
	case installConflictReinstall:
		accepted = response == "Reinstall"
	case installConflictDowngrade:
		accepted = response == "Downgrade"
	}

	if !accepted {
		s.logGUI("INFO", fmt.Sprintf("Version conflict cancelled: %s v%s", payload.Name, payload.Version))
		return
	}

	go func() {
		ctx, cleanup := s.registerTask("active")
		defer cleanup()
		s.installSinglePackage(ctx, conn, payload.Name, payload.Version, action, payload.InstalledVersion)
		s.sendInstalledList(conn)
	}()
}

func selectPackageFromIndex(index []PackageIndexItem, name string, version string) *PackageIndexItem {
	var selected *PackageIndexItem
	for i := range index {
		if !strings.EqualFold(index[i].Name, name) {
			continue
		}
		if version != "" {
			if index[i].Version == version {
				return &index[i]
			}
			continue
		}
		if selected == nil || CompareVersions(index[i].Version, selected.Version) > 0 {
			selected = &index[i]
		}
	}
	return selected
}

func (s *GUIServer) sendInstallConflictDialog(conn *websocket.Conn, action installConflictAction, pkg PackageIndexItem, installed InstalledPackage) {
	payload := installDialogPayload{
		Action:           string(action),
		Name:             pkg.Name,
		Version:          pkg.Version,
		InstalledVersion: installed.Version,
	}

	title := "Version Conflict"
	message := fmt.Sprintf("%s v%s conflicts with installed v%s.", pkg.Name, pkg.Version, installed.Version)
	options := []string{"Cancel"}
	switch action {
	case installConflictReinstall:
		title = "Already Installed"
		message = fmt.Sprintf("%s v%s is already installed. Reinstalling will replace current files and scripts.", pkg.Name, pkg.Version)
		options = []string{"Reinstall", "Cancel"}
	case installConflictDowngrade:
		title = "Version Downgrade"
		message = fmt.Sprintf("%s v%s is older than installed v%s. Downgrading may replace newer files and scripts.", pkg.Name, pkg.Version, installed.Version)
		options = []string{"Downgrade", "Cancel"}
	}

	s.logGUI("WARN", fmt.Sprintf("%s: %s", title, message))
	s.send(conn, WSResponse{
		Type: "dialog",
		Dialog: &DialogInfo{
			Type:    "warn",
			Title:   title,
			Message: message,
			Options: options,
			ID:      makeInstallDialogID(payload),
		},
	})
}

func makeInstallDialogID(payload installDialogPayload) string {
	data, err := json.Marshal(payload)
	if err != nil {
		return ""
	}
	return "install:" + base64.RawURLEncoding.EncodeToString(data)
}

func parseInstallDialogID(id string) (installDialogPayload, bool) {
	var payload installDialogPayload
	encoded, ok := strings.CutPrefix(id, "install:")
	if !ok || encoded == "" {
		return payload, false
	}
	data, err := base64.RawURLEncoding.DecodeString(encoded)
	if err != nil {
		return payload, false
	}
	if err := json.Unmarshal(data, &payload); err != nil {
		return payload, false
	}
	if payload.Name == "" || payload.Version == "" {
		return payload, false
	}
	switch installConflictAction(payload.Action) {
	case installConflictReinstall, installConflictDowngrade:
		return payload, true
	default:
		return payload, false
	}
}
