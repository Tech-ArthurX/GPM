package gs

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"
)

// Logger is the optional sink for LOGS and runtime diagnostics.
type Logger interface {
	Log(level, msg string)
}

// HostCaps describes what dangerous operations the host has opted in to.
type HostCaps struct {
	AllowExec      bool
	AllowRegistry  bool
	AllowService   bool
	AllowFirewall  bool
	AllowScheduled bool
	AllowVHD       bool
	AllowLink      bool
	AllowHTTP      bool
	AllowGPM       bool

	// Optional host tool locations used by PECM/WNSH/DRVI/RUNS. If empty, gs
	// searches TempResDir first and then PATH.
	TempResDir     string
	PecmdPath      string
	WinXShellPath  string
	DrvInstallPath string
}

// Interp is a gs script interpreter.
type Interp struct {
	prog      *Program
	vars      map[string]string
	caps      HostCaps
	logger    Logger
	scriptDir string

	// stdout for EXEC / LOGS output redirection.
	stdout io.Writer

	mu sync.Mutex
}

// NewInterp builds a fresh interpreter.
func NewInterp(prog *Program, scriptDir string, caps HostCaps, logger Logger) *Interp {
	return &Interp{
		prog:      prog,
		vars:      make(map[string]string),
		caps:      caps,
		logger:    logger,
		scriptDir: scriptDir,
		stdout:    os.Stdout,
	}
}

// SetVar sets a local variable.
func (i *Interp) SetVar(k, v string) {
	i.mu.Lock()
	defer i.mu.Unlock()
	i.vars[k] = v
}

// GetVar reads a local variable.
func (i *Interp) GetVar(k string) (string, bool) {
	i.mu.Lock()
	defer i.mu.Unlock()
	v, ok := i.vars[k]
	return v, ok
}

func (i *Interp) log(level, msg string) {
	if i.logger != nil {
		i.logger.Log(level, msg)
	}
}

// Expand resolves %NAME% (env) and %@VAR% (local) references in s.
func (i *Interp) Expand(s string) string {
	var sb strings.Builder
	n := len(s)
	for p := 0; p < n; p++ {
		ch := s[p]
		if ch != '%' {
			sb.WriteByte(ch)
			continue
		}
		// Find closing %.
		end := strings.IndexByte(s[p+1:], '%')
		if end < 0 {
			sb.WriteByte(ch)
			continue
		}
		end += p + 1
		name := s[p+1 : end]
		if strings.HasPrefix(name, "@") {
			if v, ok := i.GetVar(name[1:]); ok {
				sb.WriteString(v)
			}
		} else {
			sb.WriteString(os.Getenv(name))
		}
		p = end
	}
	return sb.String()
}

// expandArgs applies Expand to every arg, returning a copy.
func (i *Interp) expandArgs(args []string) []string {
	out := make([]string, len(args))
	for idx, a := range args {
		out[idx] = i.Expand(a)
	}
	return out
}

// Run executes the program. If MAIN subroutine is defined it is run after
// the top-level statements; otherwise just the top level runs.
func (i *Interp) Run(ctx context.Context) error {
	if err := i.runStatements(ctx, i.prog.Main); err != nil {
		if _, ok := err.(exitSignal); ok {
			return nil
		}
		return err
	}
	if body, ok := i.prog.Subs["MAIN"]; ok {
		if err := i.runStatements(ctx, body); err != nil {
			if _, ok := err.(exitSignal); ok {
				return nil
			}
			return err
		}
	}
	return nil
}

// RunPhases executes top-level setup once and then each requested subroutine if
// present. Phase names are normalized to uppercase and may use common aliases
// such as PREI/POSTU.
func (i *Interp) RunPhases(ctx context.Context, phases []string) error {
	if err := i.runStatements(ctx, i.prog.Main); err != nil {
		if _, ok := err.(exitSignal); ok {
			return nil
		}
		return err
	}
	for _, phase := range phases {
		name := normalizePhaseName(phase)
		body, ok := i.prog.Subs[name]
		if !ok {
			continue
		}
		i.log("INFO", "gs phase: "+name)
		if err := i.runStatements(ctx, body); err != nil {
			if _, ok := err.(exitSignal); ok {
				return nil
			}
			return err
		}
	}
	return nil
}

func normalizePhaseName(name string) string {
	switch strings.ToUpper(strings.TrimSpace(name)) {
	case "PREINST", "PREI", "PRE", "PREINSTALL", "PRE-INSTALL":
		return "PREINST"
	case "INSTALLING", "INSTALL", "INST", "DURING", "DOING":
		return "INSTALLING"
	case "POSTINST", "POST", "POSTINSTALL", "POST-INSTALL":
		return "POSTINST"
	case "PREUNINST", "PREU", "PREUNINSTALL", "PRE-UNINSTALL":
		return "PREUNINST"
	case "UNINSTALLING", "UNINSTALL", "UNINST", "UNIN":
		return "UNINSTALLING"
	case "POSTUNINST", "POSTU", "POSTUNINSTALL", "POST-UNINSTALL":
		return "POSTUNINST"
	default:
		return strings.ToUpper(strings.TrimSpace(name))
	}
}

type exitSignal struct{ code int }

func (e exitSignal) Error() string { return fmt.Sprintf("EXIT %d", e.code) }

func (i *Interp) runStatements(ctx context.Context, stmts []Statement) error {
	for _, s := range stmts {
		select {
		case <-ctx.Done():
			return ctx.Err()
		default:
		}
		if err := i.runOne(ctx, s); err != nil {
			if _, ok := err.(exitSignal); ok {
				return err
			}
			return fmt.Errorf("line %d: %s", s.Line, formatRuntimeError(s.Cmd, err))
		}
	}
	return nil
}

func formatRuntimeError(cmd string, err error) string {
	if err == nil {
		return ""
	}
	msg := err.Error()
	prefix := strings.ToUpper(strings.TrimSpace(cmd)) + ": "
	if strings.HasPrefix(strings.ToUpper(msg), prefix) {
		return msg
	}
	return strings.TrimSpace(cmd) + ": " + msg
}

func (i *Interp) runOne(ctx context.Context, s Statement) error {
	args := i.expandArgs(s.Args)
	switch s.Cmd {
	// --- control flow ---
	case "EXIT":
		code := 0
		if len(args) > 0 {
			if n, err := strconv.Atoi(args[0]); err == nil {
				code = n
			}
		}
		return exitSignal{code: code}
	case "WAIT":
		if len(args) == 0 {
			return nil
		}
		ms, err := strconv.Atoi(args[0])
		if err != nil {
			return fmt.Errorf("WAIT: not an integer: %s", args[0])
		}
		select {
		case <-time.After(time.Duration(ms) * time.Millisecond):
		case <-ctx.Done():
			return ctx.Err()
		}
		return nil
	case "CALL":
		if len(args) == 0 {
			return fmt.Errorf("CALL: missing name")
		}
		name := strings.ToUpper(args[0])
		body, ok := i.prog.Subs[name]
		if !ok {
			return fmt.Errorf("CALL: unknown subroutine %s", args[0])
		}
		return i.runStatements(ctx, body)

	// --- inline conditional ---
	case "IFEX":
		if len(args) < 2 {
			return fmt.Errorf("IFEX: need COND,CMD,ARGS")
		}
		result, err := evalBool(args[0], i.GetVar)
		if err != nil {
			return fmt.Errorf("IFEX: %v", err)
		}
		if result {
			inner := Statement{Cmd: args[1], Args: args[2:], Line: s.Line}
			return i.runOne(ctx, inner)
		}
		return nil

	// --- block control flow: WHEN / LOOP / FORX ---
	case "WHEN", "LOOP", "FORX":
		if len(args) < 2 {
			return fmt.Errorf("%s: missing block arguments", s.Cmd)
		}
		synthName := args[0]
		body, ok := i.prog.Blocks[synthName]
		if !ok || body == nil {
			return fmt.Errorf("%s: block body not found for %s", s.Cmd, synthName)
		}
		switch s.Cmd {
		case "WHEN":
			result, err := evalBool(args[1], i.GetVar)
			if err != nil {
				return fmt.Errorf("WHEN: %v", err)
			}
			if result {
				return i.runStatements(ctx, body)
			}
			return nil
		case "LOOP":
			n, err := strconv.Atoi(args[1])
			if err != nil {
				return fmt.Errorf("LOOP: not an integer: %s", args[1])
			}
			for j := 0; j < n; j++ {
				i.SetVar("INDEX", strconv.Itoa(j))
				if err := i.runStatements(ctx, body); err != nil {
					return err
				}
			}
			return nil
		case "FORX":
			if len(args) < 3 {
				return fmt.Errorf("FORX: need PATTERN,DIR")
			}
			pattern := args[1]
			dir := i.resolvePath(args[2])
			matches, err := filepath.Glob(filepath.Join(dir, pattern))
			if err != nil {
				return fmt.Errorf("FORX: bad pattern %s: %v", pattern, err)
			}
			for _, match := range matches {
				i.SetVar("FILE", filepath.Base(match))
				if err := i.runStatements(ctx, body); err != nil {
					return err
				}
			}
			return nil
		}

	// --- variables ---
	case "SETV":
		k, v := splitKV(args)
		if k == "" {
			return fmt.Errorf("SETV: need KEY=VALUE")
		}
		i.SetVar(k, v)
		return nil
	case "ENVI":
		k, v := splitKV(args)
		if k == "" {
			return fmt.Errorf("ENVI: need KEY=VALUE")
		}
		return os.Setenv(k, v)
	case "CALC":
		k, v := splitKV(args)
		if k == "" {
			return fmt.Errorf("CALC: need KEY=VALUE")
		}
		result, err := calcEval(v, i.GetVar)
		if err != nil {
			return fmt.Errorf("CALC: %v", err)
		}
		i.SetVar(k, result)
		return nil

	// --- strings ---
	case "STRL":
		return i.cmdStrl(args)
	case "LPOS":
		return i.cmdPos(args, true)
	case "RPOS":
		return i.cmdPos(args, false)
	case "LSTR":
		return i.cmdSubstr(args, 'L')
	case "RSTR":
		return i.cmdSubstr(args, 'R')
	case "MSTR":
		return i.cmdSubstr(args, 'M')
	case "RGEX":
		return i.cmdRegex(args)
	case "RGSB":
		return i.cmdRegexSub(args)

	// --- file/dir ---
	case "EXEC":
		return i.cmdExec(ctx, args)
	case "RUNS":
		return i.cmdRunScripts(ctx, args)
	case "PECM":
		return i.cmdPecmd(ctx, args)
	case "WNSH":
		return i.cmdWinXShell(ctx, args)
	case "DRVI":
		return i.cmdDrvInstall(ctx, args)
	case "FILE":
		return i.cmdFile(args)
	case "FDIR":
		return i.cmdDir(args)
	case "LINK":
		return i.cmdLink(args)
	case "FEXT":
		return i.cmdFext(args)
	case "FDRV":
		return i.cmdFdrv(args)
	case "EXIST":
		return i.cmdExist(args)

	// --- VHD ---
	case "VHDM":
		return i.cmdVhdMount(ctx, args)
	case "VHDU":
		return i.cmdVhdUnmount(ctx, args)
	case "VHDC":
		return i.cmdVhdCreate(ctx, args)

	// --- JSON ---
	case "JSON":
		return i.cmdJsonRead(args)
	case "JSNS":
		return i.cmdJsonSet(args)
	case "JSNL":
		return i.cmdJsonLen(args)

	// --- XML ---
	case "XMLR":
		return i.cmdXmlRead(args)
	case "XMLW":
		return i.cmdXmlWrite(args)

	// --- HTTP ---
	case "HTTP":
		return i.cmdHttp(ctx, args)
	case "DOWN":
		return i.cmdDownload(ctx, args)
	case "UPLD":
		return i.cmdUpload(ctx, args)

	// --- crypto ---
	case "HASH":
		return i.cmdHash(args)
	case "BASE":
		return i.cmdBase64(args)
	case "HEXC":
		return i.cmdHex(args)
	case "AESC":
		return i.cmdAes(args)

	// --- archive ---
	case "ZIPX":
		return i.cmdZipExtract(args)
	case "ZIPC":
		return i.cmdZipCreate(args)
	case "TARX":
		return i.cmdTarExtract(args)

	// --- system (Windows) ---
	case "SERV":
		return i.cmdService(ctx, args)
	case "TASK":
		return i.cmdTask(ctx, args)
	case "FWAL":
		return i.cmdFirewall(ctx, args)
	case "REGI":
		return i.cmdRegistry(args)

	// --- GPM-aware ---
	case "GPMI":
		return i.cmdGpmInstall(ctx, args)
	case "GPMU":
		return i.cmdGpmUninstall(ctx, args)
	case "GPMV":
		return i.cmdGpmVersion(args)
	case "INSTALLROOT":
		if len(args) == 0 || strings.TrimSpace(strings.Join(args, ",")) == "" {
			return fmt.Errorf("INSTALLROOT: missing path")
		}
		root := strings.TrimSpace(strings.Join(args, ","))
		if effective := strings.TrimSpace(os.Getenv("GPM_INSTALL_ROOT")); effective != "" {
			root = effective
		}
		i.SetVar("INSTALLROOT", root)
		i.SetVar("INSTALL_ROOT", root)
		i.log("INFO", "INSTALLROOT: "+root)
		return nil

	// --- diagnostics ---
	case "LOGS":
		return i.cmdLogs(args)
	}

	i.log("WARN", fmt.Sprintf("gs: unknown command %s at line %d", s.Cmd, s.Line))
	return nil
}

// splitKV splits a "KEY=VALUE" style first arg into key and value, then
// joins the remaining args (already comma-split) onto value with commas.
func splitKV(args []string) (string, string) {
	if len(args) == 0 {
		return "", ""
	}
	first := args[0]
	idx := strings.Index(first, "=")
	if idx < 0 {
		return first, strings.Join(args[1:], ",")
	}
	k := first[:idx]
	v := first[idx+1:]
	if len(args) > 1 {
		v = v + "," + strings.Join(args[1:], ",")
	}
	return k, v
}
