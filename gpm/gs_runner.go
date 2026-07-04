package main

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/SECTL/GPM/gs"
)

type gsLogAdapter struct{}

func (gsLogAdapter) Log(level, msg string) {
	LogDebug("[gs:%s] %s", strings.ToUpper(level), msg)
}

func gsHostCaps(tempResDir string) gs.HostCaps {
	return gs.HostCaps{
		AllowExec:      true,
		AllowRegistry:  true,
		AllowService:   true,
		AllowFirewall:  true,
		AllowScheduled: true,
		AllowVHD:       true,
		AllowLink:      true,
		AllowHTTP:      true,
		AllowGPM:       true,
		TempResDir:     tempResDir,
	}
}

func findGsFiles(scriptDest string) []string {
	patterns := []string{
		filepath.Join(scriptDest, "*.gs"),
		filepath.Join(scriptDest, "gs"),
		filepath.Join(scriptDest, "Scripts", "*.gs"),
		filepath.Join(scriptDest, "Scripts", "gs"),
	}
	seen := map[string]bool{}
	var files []string
	for _, pattern := range patterns {
		matches, _ := filepath.Glob(pattern)
		for _, m := range matches {
			clean := filepath.Clean(m)
			if err := ensurePathWithinRoot(scriptDest, clean); err != nil {
				LogDebug("Skipping unsafe gs script path %s: %v", clean, err)
				continue
			}
			if seen[clean] {
				continue
			}
			seen[clean] = true
			files = append(files, clean)
		}
	}
	sort.Strings(files)
	return files
}

func executeInstallScripts(scriptDest string, tempResDir string) {
	_ = executeInstallScriptsContext(context.Background(), scriptDest, tempResDir)
}

func executeInstallScriptsContext(ctx context.Context, scriptDest string, tempResDir string) error {
	gsFiles := findGsFiles(scriptDest)
	if len(gsFiles) == 0 {
		return ExecuteScriptsContext(ctx, scriptDest, tempResDir)
	}
	for _, file := range gsFiles {
		if err := ctx.Err(); err != nil {
			return err
		}
		LogDebug("Running gs install phases: %s", file)
		data, err := os.ReadFile(file)
		if err != nil {
			return fmt.Errorf("gs: read %s: %w", file, err)
		}
		if err := gs.RunSourcePhasesContext(ctx, string(data), filepath.Dir(file), []string{"PREINST", "INSTALLING", "POSTINST"}, gsHostCaps(tempResDir), gsLogAdapter{}); err != nil {
			LogDebug("gs install phases failed for %s: %v", file, err)
			return err
		}
	}
	return nil
}

func executeUninstallScripts(scriptDest string, tempResDir string) bool {
	gsFiles := findGsFiles(scriptDest)
	if len(gsFiles) == 0 {
		return false
	}
	for _, file := range gsFiles {
		LogDebug("Running gs uninstall phases: %s", file)
		if err := gs.RunFilePhases(file, []string{"PREUNINST", "UNINSTALLING", "POSTUNINST"}, gsHostCaps(tempResDir), gsLogAdapter{}); err != nil {
			LogDebug("gs uninstall phases failed for %s: %v", file, err)
		}
	}
	return true
}

func gsPhaseSummary() string {
	return fmt.Sprintf("install=%s uninstall=%s", "PREINST->INSTALLING->POSTINST", "PREUNINST->UNINSTALLING->POSTUNINST")
}
