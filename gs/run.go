package gs

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

// RunFile parses and executes a .gs script file.
// scriptDir is used to resolve relative paths and as EXEC working directory.
// caps controls what dangerous operations are allowed.
// logger receives LOGS output and runtime diagnostics; may be nil.
func RunFile(path string, caps HostCaps, logger Logger) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("gs: read %s: %w", path, err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Minute)
	defer cancel()
	return RunSourceContext(ctx, string(data), filepath.Dir(path), caps, logger)
}

// RunSource parses and executes gs source text.
func RunSource(src string, scriptDir string, caps HostCaps, logger Logger) error {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Minute)
	defer cancel()
	return RunSourceContext(ctx, src, scriptDir, caps, logger)
}

// RunSourceContext parses and executes gs source text with caller-controlled
// cancellation.
func RunSourceContext(ctx context.Context, src string, scriptDir string, caps HostCaps, logger Logger) error {
	prog, err := ParseString(src)
	if err != nil {
		return fmt.Errorf("gs parse: %w", err)
	}
	interp := NewInterp(prog, scriptDir, caps, logger)
	return interp.Run(ctx)
}

// RunFilePhases parses a .gs script and runs top-level setup once, then the
// named phase subroutines that exist. Missing phases are skipped, which lets a
// package provide only preinstall, only postinstall, etc.
func RunFilePhases(path string, phases []string, caps HostCaps, logger Logger) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("gs: read %s: %w", path, err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Minute)
	defer cancel()
	return RunSourcePhasesContext(ctx, string(data), filepath.Dir(path), phases, caps, logger)
}

// RunSourcePhasesContext parses a .gs script and runs selected phases with
// caller-controlled cancellation.
func RunSourcePhasesContext(ctx context.Context, src string, scriptDir string, phases []string, caps HostCaps, logger Logger) error {
	prog, err := ParseString(src)
	if err != nil {
		return fmt.Errorf("gs parse: %w", err)
	}
	interp := NewInterp(prog, scriptDir, caps, logger)
	return interp.RunPhases(ctx, phases)
}
