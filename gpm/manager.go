package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

type InstalledPackage struct {
	Name        string `json:"name"`
	Version     string `json:"version"`
	Author      string `json:"author"`
	Category    string `json:"category,omitempty"`
	InstallDate string `json:"install_date"`
	InstallRoot string `json:"install_root,omitempty"`
	CorePath    string `json:"core_path"`
	ScriptPath  string `json:"script_path"`
}

type PackageManager struct {
	ConfigFile string
	Packages   map[string]InstalledPackage // Key is Name
}

func NewPackageManager() (*PackageManager, error) {
	exePath, err := os.Executable()
	if err != nil {
		return nil, err
	}
	// Save config in the same directory as the executable
	configPath := filepath.Join(filepath.Dir(exePath), "installed_packages.json")

	pm := &PackageManager{
		ConfigFile: configPath,
		Packages:   make(map[string]InstalledPackage),
	}
	pm.Load()
	return pm, nil
}

func (pm *PackageManager) Load() {
	if _, err := os.Stat(pm.ConfigFile); os.IsNotExist(err) {
		return
	}
	file, err := os.Open(pm.ConfigFile)
	if err != nil {
		LogDebug("Failed to open config file: %v", err)
		return
	}
	defer file.Close()
	if err := json.NewDecoder(file).Decode(&pm.Packages); err != nil {
		LogDebug("Failed to decode config file: %v", err)
	}
}

func (pm *PackageManager) Save() error {
	tmp := pm.ConfigFile + ".tmp"
	file, err := os.Create(tmp)
	if err != nil {
		return err
	}
	encoder := json.NewEncoder(file)
	encoder.SetIndent("", "  ")
	if err := encoder.Encode(pm.Packages); err != nil {
		file.Close()
		os.Remove(tmp)
		return err
	}
	if err := file.Close(); err != nil {
		os.Remove(tmp)
		return err
	}
	if err := os.Rename(tmp, pm.ConfigFile); err != nil {
		os.Remove(tmp)
		return err
	}
	return nil
}

func (pm *PackageManager) Get(name string) (InstalledPackage, bool) {
	pkg, ok := pm.Packages[name]
	return pkg, ok
}

func (pm *PackageManager) Add(pkg InstalledPackage) {
	pkg.InstallDate = time.Now().Format(time.RFC3339)
	pm.Packages[pkg.Name] = pkg
	if err := pm.Save(); err != nil {
		LogDebug("Failed to save config: %v", err)
	}
}

func (pm *PackageManager) Remove(name string) error {
	if _, exists := pm.Packages[name]; !exists {
		return fmt.Errorf("package with Name %s not found", name)
	}
	delete(pm.Packages, name)
	return pm.Save()
}

func (pm *PackageManager) FindByName(name string) []InstalledPackage {
	var results []InstalledPackage
	for _, pkg := range pm.Packages {
		if strings.EqualFold(pkg.Name, name) {
			results = append(results, pkg)
		}
	}
	return results
}

func listInstalledPackages(showJson bool) {
	pm, err := NewPackageManager()
	if err != nil {
		LogError("Failed to load package manager: %v", err)
		return
	}

	if len(pm.Packages) == 0 {
		if showJson {
			fmt.Println("[]")
		} else {
			PrintLine(T("list_no_packages"))
		}
		return
	}

	if showJson {
		var list []InstalledPackage
		for _, pkg := range pm.Packages {
			list = append(list, pkg)
		}
		encoder := json.NewEncoder(os.Stdout)
		encoder.SetIndent("", "  ")
		encoder.Encode(list)
		return
	}

	PrintLine(TF("list_header", len(pm.Packages)))
	for _, pkg := range pm.Packages {
		fmt.Printf(" - %s (v%s) by %s\n", pkg.Name, pkg.Version, pkg.Author)
	}
}
