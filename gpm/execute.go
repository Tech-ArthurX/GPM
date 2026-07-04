package main

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
)

// RunScriptOrder is the canonical execution order for both install and
// uninstall scripts. Exported so uninstall can run the same sequence.
var RunScriptOrder = []string{".bat", ".cmd", ".ini", ".exe", ".reg", ".lua"}

// ExecuteScripts runs scripts in the given directory in the canonical
// install/uninstall order. It is a thin wrapper around the lower-level
// runScripts which is also used during uninstall.
func ExecuteScripts(scriptsDir string, tempResDir string) {
	ExecuteScriptsContext(context.Background(), scriptsDir, tempResDir)
}

func ExecuteScriptsContext(ctx context.Context, scriptsDir string, tempResDir string) error {
	return runScripts(ctx, scriptsDir, tempResDir, RunScriptOrder, "Starting script execution for: %s", "Script execution finished")
}

func runScripts(ctx context.Context, scriptsDir string, tempResDir string, order []string, headerFmt string, footerFmt string) error {
	LogDebug("\n--- "+headerFmt+" ---", scriptsDir)

	if _, err := os.Stat(scriptsDir); os.IsNotExist(err) {
		LogDebug("Scripts directory not found: %s", scriptsDir)
		return nil
	}

	scriptsByType := make(map[string][]string)

	err := filepath.Walk(scriptsDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			LogDebug("Error walking path %s: %v", path, err)
			return nil
		}
		if info.IsDir() {
			return nil
		}
		if err := ensurePathWithinRoot(scriptsDir, path); err != nil {
			LogDebug("Skipping unsafe script path %s: %v", path, err)
			return nil
		}

		ext := strings.ToLower(filepath.Ext(path))
		for _, fileType := range order {
			if ext == fileType {
				scriptsByType[fileType] = append(scriptsByType[fileType], path)
				break
			}
		}
		return nil
	})

	if err != nil {
		LogDebug("Error during file walk: %v", err)
	}

	// Tools paths: prefer embedded resources, then fall back to PATH/common dirs.
	pecmdPath, _ := FindToolExecutable(tempResDir, "pecmd.exe")
	winxshellPath, _ := FindToolExecutable(tempResDir, "winxshell.exe")

	// regedit.exe is system only.
	regeditPath, _ := FindSystemExecutable("regedit.exe")

	// Execute files in the specified order
	for _, fileType := range order {
		if err := ctx.Err(); err != nil {
			LogDebug("Script execution cancelled: %v", err)
			return err
		}
		if scripts, ok := scriptsByType[fileType]; ok {
			// Sort by name
			sort.Strings(scripts)

			for _, scriptPath := range scripts {
				if err := ctx.Err(); err != nil {
					LogDebug("Script execution cancelled: %v", err)
					return err
				}
				LogDebug("\n -> Found script: %s", scriptPath)
				var cmd *exec.Cmd

				fullPath, err := filepath.Abs(scriptPath)
				if err != nil {
					LogDebug("Error getting absolute path: %v", err)
					continue
				}
				workingDir := filepath.Dir(fullPath)

				switch fileType {
				case ".bat", ".cmd":
					LogDebug(" -> Executing batch file via cmd.exe...")
					cmd = exec.CommandContext(ctx, "cmd.exe", "/d", "/s", "/c", "chcp 65001 >nul && call "+quoteCmdArgument(fullPath))
					cmd.Dir = workingDir

				case ".exe":
					LogDebug(" -> Executing executable directly...")
					cmd = exec.CommandContext(ctx, fullPath)
					cmd.Dir = workingDir

				case ".ini":
					if pecmdPath != "" {
						LogDebug(" -> Loading .ini via pecmd.exe LOAD...")
						cmd = exec.CommandContext(ctx, pecmdPath, "LOAD", fullPath)
					} else {
						LogDebug("Skipping .ini script, pecmd.exe not found: %s", fullPath)
						continue
					}
					cmd.Dir = workingDir

				case ".reg":
					if regeditPath != "" {
						LogDebug(" -> Importing .reg via regedit.exe...")
						cmd = exec.CommandContext(ctx, regeditPath, "/s", fullPath)
					} else {
						LogDebug("Skipping .reg script, regedit.exe not found: %s", fullPath)
						continue
					}
					cmd.Dir = workingDir

				case ".lua":
					if winxshellPath != "" {
						LogDebug(" -> Executing Lua script via winxshell.exe...")
						cmd = exec.CommandContext(ctx, winxshellPath, "-script", fullPath)
						cmd.Dir = workingDir
					} else {
						LogDebug("Skipping .lua script, winxshell.exe not found: %s", fullPath)
						continue
					}

				default:
					continue
				}

				if cmd != nil {
					// We run synchronously to match alltest.go logic and ensure installation integrity
					cmd.Stdout = os.Stdout
					cmd.Stderr = os.Stderr

					LogDebug(" -> Running...")
					if err := cmd.Run(); err != nil {
						if ctx.Err() != nil {
							LogDebug("Script cancelled '%s': %v", fullPath, ctx.Err())
							return ctx.Err()
						}
						LogDebug("Error executing '%s': %v", fullPath, err)
					} else {
						LogDebug(" -> Done.")
					}
				}
			}
		}
	}
	LogDebug("\n--- " + footerFmt + " ---")
	return nil
}

// executeScripts runs scripts in the specified directory, following the order and logic from alltest.go
func executeScripts(scriptsDir string, tempResDir string) {
	ExecuteScripts(scriptsDir, tempResDir)
}

func quoteCmdArgument(value string) string {
	return `"` + strings.ReplaceAll(value, `"`, `""`) + `"`
}
