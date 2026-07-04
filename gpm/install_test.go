package main

import (
	"archive/zip"
	"os"
	"path/filepath"
	"testing"
)

func TestUnzipBlocksZipSlip(t *testing.T) {
	dir := t.TempDir()
	zipPath := filepath.Join(dir, "bad.gpm")

	file, err := os.Create(zipPath)
	if err != nil {
		t.Fatalf("create zip: %v", err)
	}
	zw := zip.NewWriter(file)

	w, err := zw.Create("Pkg/../evil.txt")
	if err != nil {
		t.Fatalf("create entry: %v", err)
	}
	if _, err := w.Write([]byte("pwned")); err != nil {
		t.Fatalf("write entry: %v", err)
	}

	if err := zw.Close(); err != nil {
		t.Fatalf("close zip writer: %v", err)
	}
	if err := file.Close(); err != nil {
		t.Fatalf("close zip file: %v", err)
	}

	r, err := zip.OpenReader(zipPath)
	if err != nil {
		t.Fatalf("open zip: %v", err)
	}
	defer r.Close()

	coreDest := filepath.Join(dir, "core")
	scriptDest := filepath.Join(dir, "scripts")
	if err := unzip(r, coreDest, scriptDest, "Pkg"); err == nil {
		t.Fatalf("expected unzip to reject traversal path")
	}

	escaped := filepath.Join(dir, "evil.txt")
	if _, err := os.Stat(escaped); err == nil {
		t.Fatalf("zip slip wrote outside destination: %s", escaped)
	} else if !os.IsNotExist(err) {
		t.Fatalf("stat escaped path: %v", err)
	}
}

func TestUnzipSplitsOnlyTopLevelScripts(t *testing.T) {
	dir := t.TempDir()
	zipPath := filepath.Join(dir, "pkg.gpm")
	createTestZip(t, zipPath, map[string]string{
		"Pkg/Scripts/install.gs":     "install",
		"Pkg/App/Scripts/config.txt": "core-script-named-dir",
		"Pkg/bin/app.exe":            "exe",
	})

	r, err := zip.OpenReader(zipPath)
	if err != nil {
		t.Fatalf("open zip: %v", err)
	}
	defer r.Close()

	coreDest := filepath.Join(dir, "core")
	scriptDest := filepath.Join(dir, "scripts")
	if err := unzip(r, coreDest, scriptDest, "Pkg"); err != nil {
		t.Fatalf("unzip: %v", err)
	}

	assertFileContent(t, filepath.Join(scriptDest, "install.gs"), "install")
	assertFileContent(t, filepath.Join(coreDest, "App", "Scripts", "config.txt"), "core-script-named-dir")
	assertFileContent(t, filepath.Join(coreDest, "bin", "app.exe"), "exe")
}

func TestSafePackageNameRejectsPathInjection(t *testing.T) {
	bad := []string{
		"..",
		`..\evil`,
		`C:\evil`,
		`bad/name`,
		`NUL`,
	}
	for _, name := range bad {
		if _, err := safePackageName(name); err == nil {
			t.Fatalf("safePackageName(%q) returned nil error", name)
		}
	}
}

func TestPackageInstallRootDirective(t *testing.T) {
	dir := t.TempDir()
	customRoot := filepath.Join(dir, "Apps Root")
	zipPath := filepath.Join(dir, "pkg.gpm")
	createTestZip(t, zipPath, map[string]string{
		`Pkg/Scripts/install.gs`: `INSTALLROOT "` + customRoot + `"`,
		`Pkg/bin/app.exe`:        "exe",
	})

	r, err := zip.OpenReader(zipPath)
	if err != nil {
		t.Fatalf("open zip: %v", err)
	}
	defer r.Close()

	got, err := findPackageInstallRootDirective(r, "Pkg")
	if err != nil {
		t.Fatalf("find installroot: %v", err)
	}
	if got != filepath.Clean(customRoot) {
		t.Fatalf("installroot = %q, want %q", got, filepath.Clean(customRoot))
	}
}

func TestPackageInstallRootDirectiveRequiresAbsolutePath(t *testing.T) {
	if _, err := parseInstallRootDirective(`INSTALLROOT relative\apps`); err == nil {
		t.Fatalf("expected relative INSTALLROOT to be rejected")
	}
}

func TestResolveInstallRootPrefersUserConfigOverPackageDefault(t *testing.T) {
	dir := t.TempDir()
	envRoot := filepath.Join(dir, "env-root")
	packageRoot := filepath.Join(dir, "package-root")
	t.Setenv("GPM_INSTALL_ROOT", envRoot)
	t.Setenv("HPM_INSTALL_ROOT", "")
	t.Setenv("GPM_HOME", "")
	t.Setenv("HPM_INI", filepath.Join(dir, "missing.ini"))

	got := resolveInstallRootWithPackageDefault(packageRoot)
	if got != envRoot {
		t.Fatalf("install root = %q, want env override %q", got, envRoot)
	}
}

func TestResolveInstallRootUsesPackageDefaultWithoutUserConfig(t *testing.T) {
	dir := t.TempDir()
	packageRoot := filepath.Join(dir, "package-root")
	t.Setenv("GPM_INSTALL_ROOT", "")
	t.Setenv("HPM_INSTALL_ROOT", "")
	t.Setenv("GPM_HOME", "")
	t.Setenv("HPM_INI", filepath.Join(dir, "missing.ini"))

	got := resolveInstallRootWithPackageDefault(packageRoot)
	if got != packageRoot {
		t.Fatalf("install root = %q, want package default %q", got, packageRoot)
	}
}

func createTestZip(t *testing.T, zipPath string, entries map[string]string) {
	t.Helper()
	file, err := os.Create(zipPath)
	if err != nil {
		t.Fatalf("create zip: %v", err)
	}
	zw := zip.NewWriter(file)
	for name, content := range entries {
		w, err := zw.Create(name)
		if err != nil {
			t.Fatalf("create entry %s: %v", name, err)
		}
		if _, err := w.Write([]byte(content)); err != nil {
			t.Fatalf("write entry %s: %v", name, err)
		}
	}
	if err := zw.Close(); err != nil {
		t.Fatalf("close zip writer: %v", err)
	}
	if err := file.Close(); err != nil {
		t.Fatalf("close zip file: %v", err)
	}
}

func assertFileContent(t *testing.T, path string, want string) {
	t.Helper()
	got, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	if string(got) != want {
		t.Fatalf("%s content = %q, want %q", path, string(got), want)
	}
}
