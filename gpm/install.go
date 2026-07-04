package main

import (
	"archive/zip"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"strings"
)

// PackageMetadata represents the metadata stored in zip comment
type PackageMetadata struct {
	Name        string `json:"name"`
	Version     string `json:"version"`
	Author      string `json:"author"`
	Category    string `json:"category,omitempty"`
	Description string `json:"description"`
}

const SCRIPT_DIR_NAME = "Scripts"

// isScriptFile checks if a file path within the ZIP archive points to a script file.
func isScriptFile(zipPath string) bool {
	normalizedPath := strings.ReplaceAll(zipPath, "\\", "/")
	normalizedPath = strings.TrimPrefix(normalizedPath, "./")
	return strings.HasPrefix(strings.ToLower(normalizedPath), strings.ToLower(SCRIPT_DIR_NAME)+"/")
}

func safePackageName(name string) (string, error) {
	return safeSinglePathComponent(name, "package name")
}

func splitPackageEntry(packageName string, zipPath string) (string, bool, error) {
	normalizedPath := strings.ReplaceAll(zipPath, "\\", "/")
	normalizedPath = strings.TrimPrefix(normalizedPath, "./")

	if packageName != "" {
		prefix := packageName + "/"
		switch {
		case normalizedPath == packageName:
			normalizedPath = ""
		case strings.HasPrefix(normalizedPath, prefix):
			normalizedPath = strings.TrimPrefix(normalizedPath, prefix)
		case strings.HasPrefix(normalizedPath, packageName) && len(normalizedPath) > len(packageName) &&
			(normalizedPath[len(packageName)] == '/' || normalizedPath[len(packageName)] == '\\'):
			normalizedPath = normalizedPath[len(packageName)+1:]
		}
	}

	if normalizedPath == "" {
		return "", false, nil
	}

	scriptPrefix := strings.ToLower(SCRIPT_DIR_NAME) + "/"
	if strings.HasPrefix(strings.ToLower(normalizedPath), scriptPrefix) {
		rel := normalizedPath[len(SCRIPT_DIR_NAME)+1:]
		return rel, true, nil
	}

	return normalizedPath, false, nil
}

// install returns (installed, error)
func install(hpmPath string, tempResDir string, autoConfirm bool) (bool, error) {
	return installContext(context.Background(), hpmPath, tempResDir, autoConfirm)
}

func installContext(ctx context.Context, hpmPath string, tempResDir string, autoConfirm bool) (bool, error) {
	_ = autoConfirm
	if err := ctx.Err(); err != nil {
		return false, err
	}
	LogDebug("\n=== Installing Package: %s ===\n", hpmPath)

	// 1. Open Zip
	r, err := zip.OpenReader(hpmPath)
	if err != nil {
		return false, fmt.Errorf("failed to open zip file: %v", err)
	}
	defer r.Close()
	if err := ctx.Err(); err != nil {
		return false, err
	}

	// 2. Parse Metadata
	comment := []byte(r.Comment)
	LogDebug("Raw Comment: %s", string(comment))
	if len(comment) >= 3 && comment[0] == 0xef && comment[1] == 0xbb && comment[2] == 0xbf {
		comment = comment[3:]
	}

	var metadata PackageMetadata
	if err := json.Unmarshal(comment, &metadata); err != nil {
		LogDebug("UTF-8 JSON parse failed, trying GBK...")
		if gbkStr, errGBK := DecodeGBK([]byte(r.Comment)); errGBK == nil {
			gbkStr = strings.TrimPrefix(gbkStr, "\ufeff")
			if err2 := json.Unmarshal([]byte(gbkStr), &metadata); err2 != nil {
				LogDebug("Warning: Metadata parse failed: %v.", err)
			}
		}
	}

	// Metadata corruption must not be papered over. A signed package that
	// lacks a usable manifest is almost always a packaging bug or a
	// tampering attempt; recording it as v0.0.0 silently corrupts the
	// installed-package list.
	if metadata.Name == "" {
		return false, fmt.Errorf("package metadata is missing or unreadable: name is empty in zip comment")
	}
	if metadata.Version == "" {
		return false, fmt.Errorf("package metadata is missing or unreadable: version is empty in zip comment")
	}
	if metadata.Author == "" {
		return false, fmt.Errorf("package metadata is missing or unreadable: author is empty in zip comment")
	}
	packageDirName, err := safePackageName(metadata.Name)
	if err != nil {
		return false, fmt.Errorf("unsafe package metadata name %q: %w", metadata.Name, err)
	}

	LogDebug("Info: %s (v%s) by %s\n", metadata.Name, metadata.Version, metadata.Author)

	// 3. Determine Install Paths (before any destructive work)
	packageInstallRoot, err := findPackageInstallRootDirective(r, metadata.Name)
	if err != nil {
		return false, fmt.Errorf("failed to read INSTALLROOT directive: %w", err)
	}
	programFilesPath := resolveInstallRootWithPackageDefault(packageInstallRoot)

	coreDest := filepath.Join(programFilesPath, packageDirName)

	exePath, err := os.Executable()
	if err != nil {
		return false, fmt.Errorf("failed to get executable path: %v", err)
	}
	exeDir := filepath.Dir(exePath)
	scriptDest := filepath.Join(exeDir, SCRIPT_DIR_NAME, packageDirName)

	LogDebug("Core Path: %s\n", coreDest)
	LogDebug("Script Path: %s\n", scriptDest)

	// 4. Check installed version and confirm BEFORE touching the disk
	pm, err := NewPackageManager()
	if err == nil {
		if installedPkg, ok := pm.Get(metadata.Name); ok {
			action := "Reinstall"
			if installedPkg.Version != metadata.Version {
				if CompareVersions(installedPkg.Version, metadata.Version) < 0 {
					action = "Upgrade"
				} else {
					action = "Downgrade"
				}
			}

			LogDebug("Package '%s' is already installed.", metadata.Name)
			LogDebug("  Action:            %s", action)
			LogDebug("  Installed Version: %s", installedPkg.Version)
			LogDebug("  New Version:       %s", metadata.Version)

			if !autoConfirm {
				PrintAlways("Do you want to continue? [y/N]: ")

				var response string
				fmt.Scanln(&response)
				response = strings.ToLower(strings.TrimSpace(response))
				if response != "y" && response != "yes" {
					LogDebug("Skipping installation of '%s'.", metadata.Name)
					return false, nil // Skipped
				}
			} else {
				LogDebug("Auto-confirming overwrite (-y).")
			}
		}
	}

	// 5. Cleanup old dirs (any failure here is fatal \u2014 half-removed
	// state would corrupt the next install attempt).
	if _, err := os.Stat(coreDest); err == nil {
		if err := ctx.Err(); err != nil {
			return false, err
		}
		LogDebug("Removing old core directory...")
		if err := os.RemoveAll(coreDest); err != nil {
			return false, fmt.Errorf("failed to remove old core directory %s: %v", coreDest, err)
		}
	}
	if _, err := os.Stat(scriptDest); err == nil {
		if err := ctx.Err(); err != nil {
			return false, err
		}
		LogDebug("Removing old script directory...")
		if err := os.RemoveAll(scriptDest); err != nil {
			return false, fmt.Errorf("failed to remove old script directory %s: %v", scriptDest, err)
		}
	}

	// 6. Extract Files
	LogDebug("Extracting files...")
	if err := unzip(r, coreDest, scriptDest, metadata.Name); err != nil {
		return false, fmt.Errorf("failed to extract files: %v", err)
	}
	if err := ctx.Err(); err != nil {
		return false, err
	}
	LogDebug("Extraction complete.")

	// 7. Execute Scripts
	var scriptErr error
	withInstallEnvironment(programFilesPath, coreDest, scriptDest, func() {
		scriptErr = executeInstallScriptsContext(ctx, scriptDest, tempResDir)
	})
	if scriptErr != nil {
		return false, scriptErr
	}
	if err := ctx.Err(); err != nil {
		return false, err
	}

	// 8. Record installation
	if pm != nil {
		pm.Add(InstalledPackage{
			Name:        metadata.Name,
			Version:     metadata.Version,
			Author:      metadata.Author,
			Category:    metadata.Category,
			InstallRoot: programFilesPath,
			CorePath:    coreDest,
			ScriptPath:  scriptDest,
		})
	}

	LogDebug("\n=== Installation Complete ===")
	return true, nil
}

func unzip(r *zip.ReadCloser, coreDest string, scriptDest string, packageName string) error {
	for _, f := range r.File {
		// Decode filename
		name := f.Name
		if f.NonUTF8 {
			if gbkName, err := DecodeGBK([]byte(name)); err == nil {
				name = gbkName
			}
		}

		relName, isScript, err := splitPackageEntry(packageName, name)
		if err != nil {
			return err
		}
		if relName == "" {
			continue
		}

		// Determine destination
		var destBase string
		if isScript {
			destBase = scriptDest
		} else {
			destBase = coreDest
		}

		fpath, err := safeZipDestination(destBase, relName)
		if err != nil {
			return err
		}

		if f.FileInfo().IsDir() {
			if err := os.MkdirAll(fpath, 0755); err != nil {
				return err
			}
			continue
		}
		if f.Mode()&os.ModeType != 0 {
			return fmt.Errorf("blocked unsupported zip entry type %q", name)
		}

		if err := os.MkdirAll(filepath.Dir(fpath), 0755); err != nil {
			return err
		}

		outFile, err := os.OpenFile(fpath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, sanitizeFileMode(f.Mode()))
		if err != nil {
			return err
		}

		rc, err := f.Open()
		if err != nil {
			outFile.Close()
			return err
		}

		_, err = io.Copy(outFile, rc)

		outFile.Close()
		rc.Close()

		if err != nil {
			return err
		}
	}
	return nil
}

func findPackageInstallRootDirective(r *zip.ReadCloser, packageName string) (string, error) {
	for _, f := range r.File {
		if f.FileInfo().IsDir() {
			continue
		}

		name := f.Name
		if f.NonUTF8 {
			if gbkName, err := DecodeGBK([]byte(name)); err == nil {
				name = gbkName
			}
		}

		relName, isScript, err := splitPackageEntry(packageName, name)
		if err != nil {
			return "", err
		}
		if !isScript || !isGsScriptName(relName) {
			continue
		}

		rc, err := f.Open()
		if err != nil {
			return "", err
		}
		data, readErr := io.ReadAll(io.LimitReader(rc, 1024*1024))
		closeErr := rc.Close()
		if readErr != nil {
			return "", readErr
		}
		if closeErr != nil {
			return "", closeErr
		}

		root, err := parseInstallRootDirective(string(data))
		if err != nil {
			return "", fmt.Errorf("%s: %w", name, err)
		}
		if root != "" {
			return root, nil
		}
	}
	return "", nil
}

func isGsScriptName(relName string) bool {
	normalized := strings.TrimPrefix(strings.ReplaceAll(relName, "\\", "/"), "./")
	base := strings.ToLower(filepath.Base(normalized))
	return base == "gs" || strings.HasSuffix(base, ".gs")
}

func parseInstallRootDirective(script string) (string, error) {
	for _, raw := range strings.Split(script, "\n") {
		line := strings.TrimSpace(strings.TrimPrefix(raw, "\ufeff"))
		if line == "" || strings.HasPrefix(line, ";") || strings.HasPrefix(line, "#") || strings.HasPrefix(line, "//") {
			continue
		}

		cmd, rest := splitDirectiveCommand(line)
		if !strings.EqualFold(cmd, "INSTALLROOT") {
			continue
		}

		root := strings.TrimLeft(rest, " \t,=")
		root = expandPercentEnv(stripQuotesAndBackticks(root))
		return normalizeInstallRootDirective(root)
	}
	return "", nil
}

func splitDirectiveCommand(line string) (string, string) {
	idx := strings.IndexFunc(line, func(r rune) bool {
		return r == ' ' || r == '\t' || r == ',' || r == '='
	})
	if idx < 0 {
		return line, ""
	}
	return line[:idx], line[idx:]
}

func expandPercentEnv(value string) string {
	var sb strings.Builder
	for p := 0; p < len(value); p++ {
		if value[p] != '%' {
			sb.WriteByte(value[p])
			continue
		}
		end := strings.IndexByte(value[p+1:], '%')
		if end < 0 {
			sb.WriteByte(value[p])
			continue
		}
		end += p + 1
		sb.WriteString(os.Getenv(value[p+1 : end]))
		p = end
	}
	return sb.String()
}

func normalizeInstallRootDirective(root string) (string, error) {
	root = strings.TrimSpace(root)
	if root == "" {
		return "", fmt.Errorf("INSTALLROOT requires a non-empty absolute path")
	}
	for _, r := range root {
		if r < 32 {
			return "", fmt.Errorf("INSTALLROOT contains control characters")
		}
	}

	clean := filepath.Clean(root)
	if !filepath.IsAbs(clean) {
		return "", fmt.Errorf("INSTALLROOT must be an absolute path: %s", root)
	}
	if runtime.GOOS == "windows" {
		if err := validateWindowsInstallRoot(clean); err != nil {
			return "", err
		}
	}
	return clean, nil
}

func validateWindowsInstallRoot(root string) error {
	volume := filepath.VolumeName(root)
	rest := strings.TrimPrefix(root, volume)
	if strings.Contains(rest, ":") {
		return fmt.Errorf("INSTALLROOT contains an unsafe colon outside the drive prefix")
	}
	if strings.ContainsAny(rest, "<>\"|?*") {
		return fmt.Errorf("INSTALLROOT contains invalid Windows path characters")
	}
	for _, part := range strings.FieldsFunc(rest, func(r rune) bool {
		return r == '\\' || r == '/'
	}) {
		if part == "" {
			continue
		}
		if strings.TrimSpace(part) != part {
			return fmt.Errorf("INSTALLROOT component %q has leading or trailing whitespace", part)
		}
		if strings.HasSuffix(part, ".") {
			return fmt.Errorf("INSTALLROOT component %q must not end with a dot", part)
		}
		if isWindowsReservedName(part) {
			return fmt.Errorf("INSTALLROOT component %q uses a reserved Windows name", part)
		}
	}
	return nil
}

func safeZipDestination(destBase string, zipName string) (string, error) {
	cleanName := filepath.Clean(zipName)
	if cleanName == "." || cleanName == "" {
		return "", fmt.Errorf("blocked empty zip path")
	}
	if filepath.IsAbs(cleanName) || filepath.VolumeName(cleanName) != "" || strings.Contains(cleanName, ":") ||
		cleanName == ".." || strings.HasPrefix(cleanName, ".."+string(os.PathSeparator)) {
		return "", fmt.Errorf("blocked unsafe zip path %q", zipName)
	}

	cleanBase, err := filepath.Abs(filepath.Clean(destBase))
	if err != nil {
		return "", err
	}
	target, err := filepath.Abs(filepath.Clean(filepath.Join(cleanBase, cleanName)))
	if err != nil {
		return "", err
	}
	rel, err := filepath.Rel(cleanBase, target)
	if err != nil {
		return "", err
	}
	if rel == ".." || strings.HasPrefix(rel, ".."+string(os.PathSeparator)) || filepath.IsAbs(rel) {
		return "", fmt.Errorf("blocked unsafe zip path %q", zipName)
	}
	return target, nil
}

func resolveInstallRoot() string {
	return resolveInstallRootWithPackageDefault("")
}

func resolveInstallRootWithPackageDefault(packageDefault string) string {
	if root, source := resolveConfiguredInstallRoot(); root != "" {
		LogDebug("Install root from %s: %s", source, root)
		return root
	}
	if root := strings.TrimSpace(packageDefault); root != "" {
		LogDebug("Install root from INSTALLROOT directive: %s", root)
		return root
	}
	return defaultInstallRoot()
}

func resolveConfiguredInstallRoot() (string, string) {
	if v := strings.TrimSpace(os.Getenv("GPM_INSTALL_ROOT")); v != "" {
		return v, "GPM_INSTALL_ROOT"
	}
	if v := strings.TrimSpace(os.Getenv("HPM_INSTALL_ROOT")); v != "" {
		return v, "HPM_INSTALL_ROOT"
	}
	if v := strings.TrimSpace(os.Getenv("GPM_HOME")); v != "" {
		return v, "GPM_HOME"
	}
	if cfg, err := loadServerConfig(); err == nil {
		if root := lookupInstallRootFromConfig(cfg); root != "" {
			return root, cfg.ConfigPath
		}
	}
	return "", ""
}

func defaultInstallRoot() string {
	programFilesPath := os.Getenv("PROGRAMFILES")
	if programFilesPath == "" {
		if runtime.GOOS == "windows" {
			programFilesPath = "C:\\Program Files"
		} else {
			programFilesPath = "/usr/local/bin"
		}
	}
	return programFilesPath
}

func withInstallEnvironment(installRoot string, coreDir string, scriptDir string, fn func()) {
	values := map[string]string{
		"GPM_INSTALL_ROOT": installRoot,
		"GPM_INSTALL_DIR":  coreDir,
		"GPM_CORE_DIR":     coreDir,
		"GPM_SCRIPT_DIR":   scriptDir,
	}
	type savedEnv struct {
		value string
		ok    bool
	}
	saved := make(map[string]savedEnv, len(values))
	for key, value := range values {
		old, ok := os.LookupEnv(key)
		saved[key] = savedEnv{value: old, ok: ok}
		_ = os.Setenv(key, value)
	}
	defer func() {
		for key, old := range saved {
			if old.ok {
				_ = os.Setenv(key, old.value)
			} else {
				_ = os.Unsetenv(key)
			}
		}
	}()
	fn()
}

func lookupInstallRootFromConfig(config ServerConfig) string {
	if root := lookupInstallRootInContent(config.ConfigPath); root != "" {
		return root
	}
	return ""
}

func lookupInstallRootInContent(path string) string {
	if strings.TrimSpace(path) == "" {
		return ""
	}
	content, err := os.ReadFile(path)
	if err != nil {
		return ""
	}
	return parseInstallRoot(string(content))
}

func parseInstallRoot(content string) string {
	section := ""
	for _, raw := range strings.Split(content, "\n") {
		line := strings.TrimSpace(raw)
		if line == "" {
			continue
		}
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			section = strings.ToLower(strings.TrimSpace(line[1 : len(line)-1]))
			continue
		}
		if section != "paths" {
			continue
		}
		parts := strings.SplitN(line, "=", 2)
		if len(parts) != 2 {
			continue
		}
		key := strings.ToLower(strings.TrimSpace(parts[0]))
		if key != "install_root" {
			continue
		}
		val := stripQuotesAndBackticks(strings.TrimSpace(parts[1]))
		if val != "" {
			return val
		}
	}
	return ""
}
