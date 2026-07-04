package main

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"golang.org/x/text/encoding/simplifiedchinese"
	"golang.org/x/text/transform"
)

// DecodeGBK decodes GBK data to UTF-8
func DecodeGBK(data []byte) (string, error) {
	reader := transform.NewReader(bytes.NewReader(data), simplifiedchinese.GBK.NewDecoder())
	d, err := io.ReadAll(reader)
	if err != nil {
		return "", err
	}
	return string(d), nil
}

// FindSystemExecutable finds the full path of an executable in system PATH and common locations.
func FindSystemExecutable(exeName string) (string, error) {
	// 1. Try to find in the system PATH.
	path, err := exec.LookPath(exeName)
	if err == nil {
		return path, nil
	}

	// 2. Try to find in some hardcoded common paths.
	systemRoot := os.Getenv("SystemRoot")
	programFiles := os.Getenv("ProgramFiles")

	commonPaths := []string{
		filepath.Join(systemRoot, "System32"),
		filepath.Join(programFiles, "7-Zip"),
		filepath.Join(programFiles, "WinXShell"),
	}
	for _, p := range commonPaths {
		fullPath := filepath.Join(p, exeName)
		if _, err := os.Stat(fullPath); err == nil {
			return fullPath, nil
		}
	}
	return "", fmt.Errorf("could not find executable: %s", exeName)
}

func FindToolExecutable(tempResDir string, exeName string) (string, error) {
	if strings.TrimSpace(tempResDir) != "" {
		resDir := filepath.Join(tempResDir, "res")
		fullPath := filepath.Join(resDir, exeName)
		if _, err := os.Stat(fullPath); err == nil {
			return fullPath, nil
		}
		if found := findFileCaseInsensitive(resDir, exeName); found != "" {
			return found, nil
		}
	}
	return FindSystemExecutable(exeName)
}

func findFileCaseInsensitive(dir string, fileName string) string {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return ""
	}
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		if strings.EqualFold(entry.Name(), fileName) {
			return filepath.Join(dir, entry.Name())
		}
	}
	return ""
}

func safeSinglePathComponent(value string, label string) (string, error) {
	original := value
	value = strings.TrimSpace(value)
	if value == "" {
		return "", fmt.Errorf("%s is empty", label)
	}
	if original != value {
		return "", fmt.Errorf("%s has leading or trailing whitespace", label)
	}
	if value == "." || value == ".." {
		return "", fmt.Errorf("%s is unsafe", label)
	}
	if strings.HasSuffix(value, ".") {
		return "", fmt.Errorf("%s must not end with a dot", label)
	}
	for _, r := range value {
		if r < 32 {
			return "", fmt.Errorf("%s contains control characters", label)
		}
	}
	if strings.ContainsAny(value, `\/:`) {
		return "", fmt.Errorf("%s must not contain path separators or a drive colon", label)
	}
	if strings.ContainsAny(value, "<>\"|?*") {
		return "", fmt.Errorf("%s contains invalid path characters", label)
	}
	if isWindowsReservedName(value) {
		return "", fmt.Errorf("%s uses a reserved Windows name", label)
	}
	return value, nil
}

func isWindowsReservedName(value string) bool {
	upper := strings.ToUpper(strings.TrimSpace(value))
	if idx := strings.IndexByte(upper, '.'); idx >= 0 {
		upper = upper[:idx]
	}
	switch upper {
	case "CON", "PRN", "AUX", "NUL", "CLOCK$":
		return true
	}
	if len(upper) == 4 && (strings.HasPrefix(upper, "COM") || strings.HasPrefix(upper, "LPT")) {
		switch upper[3] {
		case '1', '2', '3', '4', '5', '6', '7', '8', '9':
			return true
		}
	}
	return false
}

func packageDownloadFileName(name string) (string, error) {
	clean, err := safeSinglePathComponent(name, "package name")
	if err != nil {
		return "", err
	}
	return clean + ".gpm", nil
}

func ensurePathWithinRoot(rootPath string, targetPath string) error {
	rootAbs, err := filepath.Abs(rootPath)
	if err != nil {
		return err
	}
	targetAbs, err := filepath.Abs(targetPath)
	if err != nil {
		return err
	}
	rel, err := filepath.Rel(rootAbs, targetAbs)
	if err != nil {
		return err
	}
	if rel == "." {
		return nil
	}
	if rel == ".." || strings.HasPrefix(rel, ".."+string(os.PathSeparator)) || filepath.IsAbs(rel) {
		return fmt.Errorf("path %q escapes root %q", targetPath, rootPath)
	}
	return nil
}

func sanitizeFileMode(mode os.FileMode) os.FileMode {
	perm := mode.Perm()
	if perm == 0 {
		return 0644
	}
	return perm
}
