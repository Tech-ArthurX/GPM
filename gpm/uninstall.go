package main

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// uninstall removes an installed package by Name
func uninstall(pkgName string, autoConfirm bool) error {
	pm, err := NewPackageManager()
	if err != nil {
		return fmt.Errorf("failed to initialize package manager: %v", err)
	}

	var targetPkg InstalledPackage
	var found bool

	// 1. Try exact match
	if pkg, ok := pm.Get(pkgName); ok {
		targetPkg = pkg
		found = true
	} else {
		// 2. Try case-insensitive match
		candidates := pm.FindByName(pkgName)
		if len(candidates) == 0 {
			return fmt.Errorf("%s", TF("package_not_found", pkgName))
		}
		if len(candidates) > 1 {
			PrintLine(TF("multiple_packages", pkgName))
			for _, p := range candidates {
				// Just list name and version since UUID is gone
				PrintLine(fmt.Sprintf("  - %s (%s)", p.Name, p.Version))
			}
			return fmt.Errorf("multiple packages found, please specify exact name")
		}
		targetPkg = candidates[0]
		found = true
	}

	if !found {
		return fmt.Errorf("%s", TF("package_not_found", pkgName))
	}

	// Confirm
	PrintLine(T("uninstall_about"))
	PrintLine("  " + T("name_label") + ":    " + targetPkg.Name)
	PrintLine("  " + T("version_label") + ": " + targetPkg.Version)

	if !autoConfirm {
		PrintText(T("confirm_continue_uninstall"))
		var response string
		fmt.Scanln(&response)
		response = strings.ToLower(strings.TrimSpace(response))
		if response != "y" && response != "yes" {
			PrintLine(T("aborted"))
			return nil
		}
	}

	// Run gs uninstall phases or legacy pre-uninstall scripts BEFORE removing any
	// file, so a package can clean up registry / scheduled tasks / shortcuts that
	// live outside its own directories.
	scriptDir := targetPkg.ScriptPath
	scriptRoot, err := installedScriptRoot()
	if err != nil {
		return err
	}
	if scriptDir != "" {
		if err := ensurePathWithinRoot(scriptRoot, scriptDir); err != nil {
			return fmt.Errorf("unsafe script path in installed record: %v", err)
		}
		if !executeUninstallScripts(scriptDir, "") {
			preUninstallDir := filepath.Join(scriptDir, "Scripts")
			if _, statErr := os.Stat(preUninstallDir); statErr == nil {
				LogDebug("Running pre-uninstall scripts in: %s", preUninstallDir)
				_ = runScripts(context.Background(), preUninstallDir, "", uninstallScriptOrder, "Starting pre-uninstall scripts for: %s", "Pre-uninstall scripts finished")
			}
		}
	}

	// Delete Files
	if targetPkg.CorePath != "" {
		installRoot := strings.TrimSpace(targetPkg.InstallRoot)
		if installRoot == "" {
			installRoot = resolveInstallRoot()
		}
		if err := ensurePathWithinRoot(installRoot, targetPkg.CorePath); err != nil {
			return fmt.Errorf("unsafe core path in installed record: %v", err)
		}
	}
	LogDebug("Removing core directory: %s", targetPkg.CorePath)
	if err := os.RemoveAll(targetPkg.CorePath); err != nil {
		return fmt.Errorf("failed to remove core directory: %v", err)
	}

	if targetPkg.ScriptPath != "" {
		if err := ensurePathWithinRoot(scriptRoot, targetPkg.ScriptPath); err != nil {
			return fmt.Errorf("unsafe script path in installed record: %v", err)
		}
	}
	LogDebug("Removing script directory: %s", targetPkg.ScriptPath)
	if err := os.RemoveAll(targetPkg.ScriptPath); err != nil {
		return fmt.Errorf("failed to remove script directory: %v", err)
	}

	// Update Config
	if err := pm.Remove(targetPkg.Name); err != nil {
		return fmt.Errorf("failed to update package record: %v", err)
	}

	PrintLine(TF("uninstall_success", targetPkg.Name))
	return nil
}

// uninstallScriptOrder mirrors RunScriptOrder but skips .reg because
// uninstall packages typically don't need to delete registry entries
// through the regedit import path; .reg is still allowed for completeness.
var uninstallScriptOrder = []string{".bat", ".cmd", ".ini", ".exe", ".lua", ".reg"}

func installedScriptRoot() (string, error) {
	exePath, err := os.Executable()
	if err != nil {
		return "", fmt.Errorf("failed to get executable path: %v", err)
	}
	return filepath.Join(filepath.Dir(exePath), SCRIPT_DIR_NAME), nil
}
