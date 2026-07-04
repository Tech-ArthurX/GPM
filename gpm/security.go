package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

type RemotePackageInfo struct {
	Name        string `json:"name"`
	Version     string `json:"version"`
	Author      string `json:"author"`
	Category    string `json:"category,omitempty"`
	Description string `json:"description"`
	URL         string `json:"url"`
	Size        int64  `json:"size"`
	SHA256      string `json:"sha256,omitempty"`
	Filename    string `json:"filename,omitempty"`
}

func fetchPackageInfo(name string) (*RemotePackageInfo, error) {
	serverURL, err := getServerURL()
	if err != nil {
		return nil, err
	}

	url := fmt.Sprintf("%s/package/%s", strings.TrimRight(serverURL, "/"), name)
	LogDebug("Package Info URL: %s", url)

	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", "GPM-CLI/1.0")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("network error: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("server returned status: %d", resp.StatusCode)
	}

	var info RemotePackageInfo
	if err := json.NewDecoder(resp.Body).Decode(&info); err != nil {
		return nil, fmt.Errorf("failed to decode package info: %w", err)
	}
	return &info, nil
}

type ServerConfig struct {
	ConfigPath string
	Servers    map[string]string
	Default    string
}

func getServerURL() (string, error) {
	envURL := strings.TrimSpace(os.Getenv("HPM_SERVER_URL"))
	if envURL != "" {
		LogDebug("Using HPM_SERVER_URL: %s", envURL)
		return normalizeServerURL(envURL), nil
	}
	config, err := loadServerConfig()
	if err != nil {
		return "", err
	}
	return resolveServerURL("", config)
}

func loadServerConfig() (ServerConfig, error) {
	config := ServerConfig{
		Servers: map[string]string{},
	}
	if iniPath := strings.TrimSpace(os.Getenv("HPM_INI")); iniPath != "" {
		content, err := os.ReadFile(iniPath)
		if err != nil {
			return config, err
		}
		config.ConfigPath = iniPath
		parseIni(string(content), &config)
		LogDebug("Loaded ini: %s", config.ConfigPath)
		return config, nil
	}
	candidates := findIniCandidates()
	LogDebug("Ini candidates: %s", strings.Join(candidates, " | "))
	for _, path := range candidates {
		content, err := os.ReadFile(path)
		if err != nil {
			continue
		}
		config.ConfigPath = path
		parseIni(string(content), &config)
		LogDebug("Loaded ini: %s", config.ConfigPath)
		return config, nil
	}
	return config, nil
}

func findIniCandidates() []string {
	var candidates []string
	if cwd, err := os.Getwd(); err == nil {
		candidates = append(candidates,
			filepath.Join(cwd, "gpm.ini"),
			filepath.Join(cwd, "loader.ini"),
			filepath.Join(cwd, "..", "cli", "loader", "loader.ini"),
		)
	}
	if exePath, err := os.Executable(); err == nil {
		exeDir := filepath.Dir(exePath)
		candidates = append(candidates,
			filepath.Join(exeDir, "gpm.ini"),
			filepath.Join(exeDir, "loader.ini"),
			filepath.Join(exeDir, "..", "cli", "loader", "loader.ini"),
		)
	}
	seen := map[string]bool{}
	var unique []string
	for _, c := range candidates {
		p := filepath.Clean(c)
		if !seen[p] {
			seen[p] = true
			unique = append(unique, p)
		}
	}
	return unique
}

func parseIni(content string, config *ServerConfig) {
	section := ""
	lines := strings.Split(content, "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			section = strings.ToLower(strings.TrimSpace(line[1 : len(line)-1]))
			continue
		}
		parts := strings.SplitN(line, "=", 2)
		if len(parts) != 2 {
			continue
		}
		key := strings.TrimSpace(parts[0])
		val := stripQuotesAndBackticks(parts[1])
		if section == "servers" {
			if key != "" && val != "" {
				config.Servers[key] = val
			}
		}
		if section == "settings" && strings.EqualFold(key, "default") {
			config.Default = val
		}
	}
}

func resolveServerURL(selector string, config ServerConfig) (string, error) {
	if selector != "" {
		if idx, err := strconv.Atoi(selector); err == nil {
			val, ok := config.Servers[strconv.Itoa(idx)]
			if !ok {
				return "", fmt.Errorf("server index not found: %s", selector)
			}
			normalized := normalizeServerURL(val)
			LogDebug("Selected server index: %s -> %s", selector, normalized)
			return normalized, nil
		}
		normalized := normalizeServerURL(selector)
		LogDebug("Selected server URL: %s", normalized)
		return normalized, nil
	}
	if config.Default != "" {
		if idx, err := strconv.Atoi(config.Default); err == nil {
			val, ok := config.Servers[strconv.Itoa(idx)]
			if ok {
				normalized := normalizeServerURL(val)
				LogDebug("Default server index: %s -> %s", config.Default, normalized)
				return normalized, nil
			}
		}
		normalized := normalizeServerURL(config.Default)
		LogDebug("Default server URL: %s", normalized)
		return normalized, nil
	}
	keys := sortedServerKeys(config.Servers)
	if len(keys) == 0 {
		return "", errors.New("server URL not configured")
	}
	normalized := normalizeServerURL(config.Servers[keys[0]])
	LogDebug("Using first server index: %s -> %s", keys[0], normalized)
	return normalized, nil
}

func normalizeServerURL(value string) string {
	value = stripQuotesAndBackticks(value)
	if value == "" {
		return value
	}
	if strings.HasPrefix(value, "http://") || strings.HasPrefix(value, "https://") {
		return value
	}
	return "http://" + value
}

func stripQuotesAndBackticks(value string) string {
	value = strings.TrimSpace(value)
	if value == "" {
		return value
	}
	value = strings.Trim(value, "`\"")
	value = strings.ReplaceAll(value, "`", "")
	value = strings.ReplaceAll(value, "\"", "")
	return strings.TrimSpace(value)
}

func sortedServerKeys(servers map[string]string) []string {
	keys := make([]string, 0, len(servers))
	for k := range servers {
		keys = append(keys, k)
	}
	sort.Slice(keys, func(i, j int) bool {
		ai, errA := strconv.Atoi(keys[i])
		aj, errB := strconv.Atoi(keys[j])
		if errA == nil && errB == nil {
			return ai < aj
		}
		if errA == nil {
			return true
		}
		if errB == nil {
			return false
		}
		return keys[i] < keys[j]
	})
	return keys
}
