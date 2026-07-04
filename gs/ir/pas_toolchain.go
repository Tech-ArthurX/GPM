package ir

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

func findFPC() (string, error) {
	for _, name := range []string{"FPC_EXE", "FPC_PATH"} {
		if value := strings.TrimSpace(os.Getenv(name)); value != "" {
			if _, err := os.Stat(value); err == nil {
				return value, nil
			}
		}
	}
	for _, name := range []string{"FPC_BIN", "FPC_HOME"} {
		if value := strings.TrimSpace(os.Getenv(name)); value != "" {
			for _, p := range []string{value, filepath.Join(value, "bin"), filepath.Join(value, "bin", "x86_64-win64")} {
				candidate := filepath.Join(p, "fpc.exe")
				if _, err := os.Stat(candidate); err == nil {
					return candidate, nil
				}
			}
		}
	}
	for _, p := range []string{
		`C:\lazarus\fpc\3.2.2\bin\x86_64-win64\fpc.exe`,
		`C:\lazarus\fpc\3.2.0\bin\x86_64-win64\fpc.exe`,
		`C:\FPC\3.2.2\bin\x86_64-win64\fpc.exe`,
		`C:\FPC\3.2.0\bin\x86_64-win64\fpc.exe`,
	} {
		if _, err := os.Stat(p); err == nil {
			return p, nil
		}
	}
	if fpc, err := exec.LookPath("fpc"); err == nil {
		return fpc, nil
	}
	return "", fmt.Errorf("fpc not found; set FPC_EXE, FPC_BIN, FPC_HOME, or add fpc.exe to PATH")
}

func lazarusUnitArgs() []string {
	if raw := strings.TrimSpace(os.Getenv("FPC_UNIT_PATHS")); raw != "" {
		var args []string
		for _, part := range strings.Split(raw, string(os.PathListSeparator)) {
			if part = strings.TrimSpace(part); part != "" {
				args = append(args, "-Fu"+part)
			}
		}
		if len(args) > 0 {
			return args
		}
	}
	for _, name := range []string{"LAZARUS_HOME", "LAZARUS_ROOT", "LAZARUS_DIR"} {
		if root := strings.TrimSpace(os.Getenv(name)); root != "" {
			if args := lazarusUnitArgsFromRoot(root); len(args) > 0 {
				return args
			}
		}
	}
	for _, root := range []string{`C:\lazarus`, `C:\Program Files\Lazarus`} {
		if args := lazarusUnitArgsFromRoot(root); len(args) > 0 {
			return args
		}
	}
	return nil
}

func lazarusUnitArgsFromRoot(root string) []string {
	paths := []string{
		filepath.Join(root, "lcl", "units", "x86_64-win64"),
		filepath.Join(root, "lcl", "units", "x86_64-win64", "win32"),
		filepath.Join(root, "components", "lazutils", "lib", "x86_64-win64"),
		filepath.Join(root, "packager", "units", "x86_64-win64"),
	}
	args := make([]string, 0, len(paths))
	for _, p := range paths {
		if _, err := os.Stat(p); err == nil {
			args = append(args, "-Fu"+p)
		}
	}
	return args
}
