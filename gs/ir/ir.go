package ir

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"

	"github.com/SECTL/GPM/gs"
)

type varInfo struct {
	typ ValType
}

type Generator struct {
	prog        *gs.Program
	code        strings.Builder
	llvmPath    string
	srcDir      string
	backend     string
	optLevel    string
	vars        map[string]varInfo
	nextID      int
	exitCode    int
	cIncludes   []string
	cHeaderCode []string
	extraCFlags []string
	extraObjs   []string
	extraLibs   []string
	resultDir   string
	callStack   map[string]bool
	needNet     bool
}

func NewGenerator(prog *gs.Program, llvmPath string) *Generator {
	if llvmPath == "" {
		llvmPath = findLLVMPath()
	}
	return &Generator{prog: prog, llvmPath: llvmPath, backend: "llvm", optLevel: "2", vars: make(map[string]varInfo), nextID: 1000, callStack: make(map[string]bool)}
}

func (g *Generator) SetSourceDir(dir string)   { g.srcDir = dir }
func (g *Generator) SetBackend(backend string) { g.backend = backend }
func (g *Generator) SetOptLevel(level string)  { g.optLevel = level }

func (g *Generator) Compile(outputPath string) error {
	g.resultDir = filepath.Dir(outputPath)
	if g.backend == "lcl" {
		return g.compileLCL(outputPath)
	}
	return g.compileLLVM(outputPath)
}

func (g *Generator) generate() error {
	return g.genStatements(g.prog.Main)
}

func (g *Generator) genStatements(stmts []gs.Statement) error {
	for _, stmt := range stmts {
		cmd := cleanCmd(stmt.Cmd)
		args := cleanArgs(stmt.Args)
		switch cmd {
		case "SETV":
			g.genSet(args, TypeFloat, true)
		case "STRV":
			g.genSet(args, TypeString, false)
		case "FLOT":
			g.genSet(args, TypeFloat, false)
		case "BOOL":
			g.genSet(args, TypeBool, false)
		case "CALC":
			g.genCalc(args)
		case "CALL":
			if err := g.genCall(args); err != nil {
				return err
			}
		case "LOGS":
			g.genLogs(args)
		case "EXEC":
			g.genExec(args)
		case "WAPI", "APIC":
			g.genWinAPI(args)
		case "DLLO", "DLOP":
			g.genDllOpen(args)
		case "DLLG", "DLSY":
			g.genDllGet(args)
		case "DLLC", "DLCA":
			g.genDllCall(args)
		case "DLLF", "DLCL":
			g.genDllFree(args)
		case "UIDF":
			if err := g.genUIDef(args); err != nil {
				return err
			}
		case "UILP":
			g.genUILoop(args)
		case "MBOX":
			g.genMessageBox(args)
		case "BEEP":
			g.genBeep(args)
		case "EROR":
			g.genError(args)
		case "EXIT":
			g.genExit(args)
		case "WAIT":
			g.genWait(args)
		case "ENVI":
			g.genEnvi(args)
		case "STRL":
			g.genStrl(args)
		case "LPOS":
			g.genPos(args, "gs_lpos2")
		case "RPOS":
			g.genPos(args, "gs_rpos2")
		case "FEXT":
			g.genFext(args)
		case "FDRV":
			g.genFdrv(args)
		case "EXIST":
			g.genExist(args)
		case "IFEX":
			if err := g.genIfex(args); err != nil {
				return err
			}
		case "WHEN":
			if err := g.genWhen(args); err != nil {
				return err
			}
		case "LOOP":
			if err := g.genLoop(args); err != nil {
				return err
			}
		case "FORX":
			if err := g.genForx(args); err != nil {
				return err
			}
		case "FDIR":
			if err := g.genDir(args); err != nil {
				return err
			}
		case "LINK":
			if err := g.genLink(args); err != nil {
				return err
			}
		case "FILE":
			if err := g.genFile(args); err != nil {
				return err
			}
		case "HASH":
			g.genHash(args)
		case "BASE":
			g.genBase64(args)
		case "HEXC":
			g.genHex(args)
		case "JSON":
			if err := g.genJsonRead(args); err != nil {
				return err
			}
		case "JSNL":
			if err := g.genJsonLen(args); err != nil {
				return err
			}
		case "JSNS":
			if err := g.genJsonSet(args); err != nil {
				return err
			}
		case "REGI":
			if err := g.genRegistry(args); err != nil {
				return err
			}
		case "SERV":
			if err := g.genService(args); err != nil {
				return err
			}
		case "TASK":
			if err := g.genTask(args); err != nil {
				return err
			}
		case "FWAL":
			if err := g.genFirewall(args); err != nil {
				return err
			}
		case "HTTP":
			if err := g.genHTTP(args); err != nil {
				return err
			}
		case "DOWN":
			if err := g.genDown(args); err != nil {
				return err
			}
		case "UPLD":
			if err := g.genUpload(args); err != nil {
				return err
			}
		case "LSTR":
			g.genSubstr(args, "gs_lstr", 1)
		case "RSTR":
			g.genSubstr(args, "gs_rstr", 1)
		case "MSTR":
			g.genSubstr(args, "gs_mstr", 2)
		case "RGEX":
			g.genRegex(args)
		case "RGSB":
			g.genRegexSub(args)
		case "RUNS":
			dir := "."
			if len(args) > 0 && strings.TrimSpace(args[0]) != "" {
				dir = args[0]
			}
			fmt.Fprintf(&g.code, "    gs_run_scripts(%s);\n", g.cValue(dir, TypeString))
		case "PECM":
			op, path := "", ""
			if len(args) > 0 {
				op = args[0]
			}
			if len(args) > 1 {
				path = args[1]
			}
			fmt.Fprintf(&g.code, "    gs_pecmd(%s, %s);\n", g.cValue(op, TypeString), g.cValue(path, TypeString))
		case "WNSH":
			if len(args) > 0 {
				fmt.Fprintf(&g.code, "    gs_winxshell(%s);\n", g.cValue(args[0], TypeString))
			}
		case "VHDM":
			mode := ""
			if len(args) > 1 {
				mode = args[1]
			}
			if len(args) > 0 {
				fmt.Fprintf(&g.code, "    gs_vhd_mount(%s, %s);\n", g.cValue(args[0], TypeString), g.cValue(mode, TypeString))
			}
		case "VHDU":
			if len(args) > 0 {
				fmt.Fprintf(&g.code, "    gs_vhd_unmount(%s);\n", g.cValue(args[0], TypeString))
			}
		case "VHDC":
			if len(args) >= 3 {
				fmt.Fprintf(&g.code, "    gs_vhd_create(%s, %s, %s);\n", g.cValue(args[0], TypeString), g.cArg(args[1]), g.cValue(args[2], TypeString))
			}
		case "XMLR":
			g.genXmlRead(args)
		case "XMLW":
			g.genXmlWrite(args)
		case "AESC":
			g.genAes(args)
		case "ZIPX":
			if len(args) >= 2 {
				fmt.Fprintf(&g.code, "    gs_zip_extract(%s, %s);\n", g.cValue(args[0], TypeString), g.cValue(args[1], TypeString))
			}
		case "ZIPC":
			if len(args) >= 2 {
				fmt.Fprintf(&g.code, "    gs_zip_create(%s, %s);\n", g.cValue(args[0], TypeString), g.cValue(args[1], TypeString))
			}
		case "TARX":
			if len(args) >= 2 {
				fmt.Fprintf(&g.code, "    gs_tar_extract(%s, %s);\n", g.cValue(args[0], TypeString), g.cValue(args[1], TypeString))
			}
		case "GPMI":
			name, ver := "", ""
			if len(args) > 0 {
				name = args[0]
			}
			if len(args) > 1 {
				ver = args[1]
			}
			fmt.Fprintf(&g.code, "    gs_gpm_install(%s, %s);\n", g.cValue(name, TypeString), g.cValue(ver, TypeString))
		case "GPMU":
			if len(args) > 0 {
				fmt.Fprintf(&g.code, "    gs_gpm_uninstall(%s);\n", g.cValue(args[0], TypeString))
			}
		case "GPMV":
			name, pkg := splitKV(args)
			if name != "" {
				g.declare(name, TypeString)
				fmt.Fprintf(&g.code, "    %s = gs_gpm_version(%s);\n", name, g.cValue(pkg, TypeString))
			}
		default:
			if cmd != "" {
				return fmt.Errorf("line %d: compiler does not support command %q", stmt.Line, cmd)
			}
		}
	}
	return nil
}

func cleanCmd(s string) string {
	s = strings.TrimPrefix(s, "\ufeff")
	return strings.ToUpper(strings.TrimSpace(s))
}

func cleanArgs(args []string) []string {
	out := make([]string, len(args))
	for i, a := range args {
		out[i] = strings.TrimSpace(a)
	}
	return out
}

func (g *Generator) genSet(args []string, forced ValType, infer bool) {
	name, value := splitKV(args)
	if name == "" {
		return
	}
	typ := forced
	if infer {
		typ = inferType(value)
	}
	g.declare(name, typ)
	fmt.Fprintf(&g.code, "    %s = %s;\n", name, g.cValue(value, typ))
}

func (g *Generator) genCalc(args []string) {
	name, expr := splitKV(args)
	if name == "" {
		return
	}
	g.declare(name, TypeFloat)
	fmt.Fprintf(&g.code, "    %s = %s;\n", name, g.cExpr(expr))
}

func (g *Generator) genLogs(args []string) {
	msg := joinPayload(args)
	vars := g.extractVars(msg)
	if len(vars) == 0 {
		fmt.Fprintf(&g.code, "    printf(\"%s\\n\");\n", cEscape(msg))
		return
	}
	format := cEscape(msg)
	for _, v := range vars {
		format = strings.Replace(format, "%"+v+"%", g.vars[v].typ.printfFmt(), 1)
		format = strings.Replace(format, "%@"+v+"%", g.vars[v].typ.printfFmt(), 1)
	}
	fmt.Fprintf(&g.code, "    printf(\"%s\\n\"", format)
	for _, v := range vars {
		fmt.Fprintf(&g.code, ", %s", g.vars[v].typ.printfExpr(v))
	}
	g.code.WriteString(");\n")
}

func (g *Generator) genExec(args []string) {
	if len(args) == 0 {
		return
	}
	mode := strings.ToUpper(strings.TrimSpace(args[0]))
	switch mode {
	case "WAIT":
		cmd := strings.Join(args[1:], ",")
		if cmd != "" {
			fmt.Fprintf(&g.code, "    system(\"%s\");\n", cEscape(cmd))
		}
	case "NOWAIT", "ASYNC":
		cmd := strings.Join(args[1:], ",")
		if cmd != "" {
			fmt.Fprintf(&g.code, "    ShellExecuteA(NULL, \"open\", \"cmd.exe\", %s, NULL, SW_SHOWNORMAL);\n", g.cValue("/c start \"\" "+cmd, TypeString))
		}
	case "HIDE":
		cmd := strings.Join(args[1:], ",")
		if cmd != "" {
			fmt.Fprintf(&g.code, "    WinExec(%s, SW_HIDE);\n", g.cValue(cmd, TypeString))
		}
	case "MIN":
		cmd := strings.Join(args[1:], ",")
		if cmd != "" {
			fmt.Fprintf(&g.code, "    WinExec(%s, SW_SHOWMINNOACTIVE);\n", g.cValue(cmd, TypeString))
		}
	case "OPEN":
		if len(args) >= 2 {
			fmt.Fprintf(&g.code, "    ShellExecuteA(NULL, \"open\", %s, NULL, NULL, SW_SHOWNORMAL);\n", g.cValue(args[1], TypeString))
		}
	case "RUNAS":
		if len(args) >= 2 {
			params := ""
			if len(args) > 2 {
				params = strings.Join(args[2:], ",")
			}
			fmt.Fprintf(&g.code, "    ShellExecuteA(NULL, \"runas\", %s, %s, NULL, SW_SHOWNORMAL);\n", g.cValue(args[1], TypeString), g.cValue(params, TypeString))
		}
	default:
		cmd := strings.Join(args, " ")
		if cmd != "" {
			fmt.Fprintf(&g.code, "    system(\"%s\");\n", cEscape(cmd))
		}
	}
}

func (g *Generator) genWinAPI(args []string) {
	lhs, rest := splitMaybeAssign(args)
	if rest == "" {
		return
	}
	fn, callArgs := splitCall(rest)
	if fn == "" {
		return
	}
	parts := strings.Split(fn, ".")
	funcName := parts[len(parts)-1]
	if lhs != "" {
		g.declare(lhs, TypeFloat)
		fmt.Fprintf(&g.code, "    %s = (double)(long long)%s(%s);\n", lhs, funcName, g.cArgs(callArgs))
	} else {
		fmt.Fprintf(&g.code, "    %s(%s);\n", funcName, g.cArgs(callArgs))
	}
}

func (g *Generator) genDllOpen(args []string) {
	name, dll := splitKV(args)
	if name == "" {
		return
	}
	g.declare(name, TypeHandle)
	fmt.Fprintf(&g.code, "    %s = gs_dll_open(%s);\n", name, g.cValue(dll, TypeString))
}

func (g *Generator) genDllGet(args []string) {
	name, rest := splitKV(args)
	if name == "" || rest == "" {
		return
	}
	parts := splitCSV(rest)
	if len(parts) < 2 {
		return
	}
	g.declare(name, TypeProc)
	fmt.Fprintf(&g.code, "    %s = gs_dll_sym(%s, %s);\n", name, strings.TrimSpace(parts[0]), g.cValue(parts[1], TypeString))
}

func (g *Generator) genDllCall(args []string) {
	name, rest := splitKV(args)
	if name == "" || rest == "" {
		return
	}
	parts := splitCSV(rest)
	if len(parts) < 1 {
		return
	}
	proc := strings.TrimSpace(parts[0])
	callArgs := parts[1:]
	for len(callArgs) < 8 {
		callArgs = append(callArgs, "0")
	}
	g.declare(name, TypeFloat)
	fmt.Fprintf(&g.code, "    %s = gs_dll_call(%s, %s);\n", name, proc, g.cProcArgs(callArgs))
}

func (g *Generator) genDllFree(args []string) {
	if len(args) == 0 {
		return
	}
	fmt.Fprintf(&g.code, "    gs_dll_close(%s);\n", strings.TrimSpace(args[0]))
}

func (g *Generator) genMessageBox(args []string) {
	if len(args) == 0 {
		return
	}
	parts := splitCSV(strings.Join(args, ","))
	if len(parts) == 0 {
		return
	}
	text, title, kind := parts[0], "gs", "info"
	if len(parts) >= 2 {
		title = parts[1]
	}
	if len(parts) >= 3 {
		kind = strings.ToLower(strings.TrimSpace(parts[2]))
	}
	fn := "gs_show_info"
	if kind == "error" || kind == "err" {
		fn = "gs_show_error"
	}
	fmt.Fprintf(&g.code, "    %s(%s, %s);\n", fn, g.cArg(text), g.cArg(title))
}

func (g *Generator) genBeep(args []string) {
	parts := splitCSV(strings.Join(args, ","))
	if len(parts) < 2 {
		return
	}
	fmt.Fprintf(&g.code, "    gs_beep(%s, %s);\n", g.cArg(parts[0]), g.cArg(parts[1]))
}

func (g *Generator) genError(args []string) {
	parts := splitCSV(strings.Join(args, ","))
	if len(parts) == 0 {
		return
	}
	msg, title, code := parts[0], "gs error", 1
	if len(parts) >= 2 {
		title = parts[1]
	}
	if len(parts) >= 3 {
		if n, err := strconv.Atoi(strings.TrimSpace(parts[2])); err == nil {
			code = n
		}
	}
	fmt.Fprintf(&g.code, "    gs_show_error(%s, %s);\n", g.cArg(msg), g.cArg(title))
	fmt.Fprintf(&g.code, "    return %d;\n", code)
	g.exitCode = code
}

func (g *Generator) genExit(args []string) {
	code := 0
	if len(args) > 0 {
		if n, err := strconv.Atoi(strings.TrimSpace(args[0])); err == nil {
			code = n
		}
	}
	fmt.Fprintf(&g.code, "    return %d;\n", code)
	g.exitCode = code
}

func (g *Generator) genWait(args []string) {
	if len(args) == 0 {
		return
	}
	fmt.Fprintf(&g.code, "    Sleep((DWORD)(%s));\n", g.cArg(args[0]))
}

func (g *Generator) genEnvi(args []string) {
	k, v := splitKV(args)
	if k == "" {
		return
	}
	fmt.Fprintf(&g.code, "    gs_env_set(%s, %s);\n", g.cValue(k, TypeString), g.cValue(v, TypeString))
}

func (g *Generator) genStrl(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 1 {
		return
	}
	g.declare(name, TypeFloat)
	fmt.Fprintf(&g.code, "    %s = gs_strlen2(%s);\n", name, g.cValue(parts[0], TypeString))
}

func (g *Generator) genPos(args []string, fn string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return
	}
	g.declare(name, TypeFloat)
	fmt.Fprintf(&g.code, "    %s = %s(%s, %s);\n", name, fn, g.cValue(parts[0], TypeString), g.cValue(parts[1], TypeString))
}

func (g *Generator) genFext(args []string) {
	name, rest := splitKV(args)
	if name == "" || rest == "" {
		return
	}
	g.declare(name, TypeString)
	fmt.Fprintf(&g.code, "    static char %s_buf[65536]; gs_fext(%s, %s_buf); %s = %s_buf;\n", name, g.cValue(rest, TypeString), name, name, name)
}

func (g *Generator) genFdrv(args []string) {
	name, rest := splitKV(args)
	if name == "" || rest == "" {
		return
	}
	g.declare(name, TypeString)
	fmt.Fprintf(&g.code, "    static char %s_buf[65536]; gs_fdrv(%s, %s_buf); %s = %s_buf;\n", name, g.cValue(rest, TypeString), name, name, name)
}

func (g *Generator) genExist(args []string) {
	name, rest := splitKV(args)
	if name == "" {
		return
	}
	g.declare(name, TypeFloat)
	fmt.Fprintf(&g.code, "    %s = gs_file_exist(%s);\n", name, g.cValue(rest, TypeString))
}

func (g *Generator) declare(name string, typ ValType) {
	if _, ok := g.vars[name]; ok {
		return
	}
	g.vars[name] = varInfo{typ: typ}
	fmt.Fprintf(&g.code, "    %s %s = %s;\n", typ.cType(), name, typ.zeroInit())
}

func (g *Generator) extractVars(s string) []string {
	var out []string
	for i := 0; i < len(s); i++ {
		if s[i] != '%' {
			continue
		}
		start := i + 1
		if start < len(s) && s[start] == '@' {
			start++
		}
		end := strings.IndexByte(s[start:], '%')
		if end < 0 {
			continue
		}
		name := s[start : start+end]
		if _, ok := g.vars[name]; ok {
			out = append(out, name)
		}
		i = start + end
	}
	return out
}

func (g *Generator) cExpr(expr string) string {
	expr = strings.TrimSpace(expr)
	for name := range g.vars {
		expr = strings.ReplaceAll(expr, name, "("+name+")")
	}
	return expr
}

func (g *Generator) cValue(v string, typ ValType) string {
	v = strings.TrimSpace(v)
	if v == "" {
		return typ.zeroInit()
	}
	if varName, ok := g.percentVar(v); ok {
		return varName
	}
	if typ == TypeString {
		return cString(v)
	}
	if typ == TypeBool {
		l := strings.ToLower(v)
		if l == "true" || l == "1" {
			return "1"
		}
		return "0"
	}
	return g.cArg(v)
}

func (g *Generator) cStringExpr(v string) string {
	v = strings.TrimSpace(v)
	if v == "" {
		return "\"\""
	}
	if name, ok := g.percentVar(v); ok && g.vars[name].typ == TypeString {
		return name
	}
	format, args := g.printfStringExpr(v)
	if len(args) == 0 {
		return cString(v)
	}
	buf := g.nextTemp("str")
	fmt.Fprintf(&g.code, "    static char %s[65536]; snprintf(%s, sizeof(%s), \"%s\"", buf, buf, buf, cEscape(format))
	for _, name := range args {
		fmt.Fprintf(&g.code, ", %s", g.vars[name].typ.printfExpr(name))
	}
	g.code.WriteString(");\n")
	return buf
}

func (g *Generator) printfStringExpr(v string) (string, []string) {
	var format strings.Builder
	var args []string
	for i := 0; i < len(v); i++ {
		if v[i] != '%' {
			format.WriteByte(v[i])
			continue
		}
		start := i + 1
		if start < len(v) && v[start] == '@' {
			start++
		}
		endRel := strings.IndexByte(v[start:], '%')
		if endRel < 0 {
			format.WriteString("%%")
			continue
		}
		name := v[start : start+endRel]
		if info, ok := g.vars[name]; ok {
			format.WriteString(info.typ.printfFmt())
			args = append(args, name)
			i = start + endRel
			continue
		}
		format.WriteString("%%")
	}
	return format.String(), args
}

func (g *Generator) cArg(v string) string {
	v = strings.TrimSpace(v)
	if v == "" {
		return "0"
	}
	if varName, ok := g.percentVar(v); ok {
		return varName
	}
	if _, ok := g.vars[v]; ok {
		return v
	}
	if strings.EqualFold(v, "true") {
		return "1"
	}
	if strings.EqualFold(v, "false") {
		return "0"
	}
	if v == "NULL" || v == "0" {
		return v
	}
	if isNumber(v) {
		return v
	}
	if looksLikeCConst(v) {
		return v
	}
	return cString(v)
}

func (g *Generator) percentVar(v string) (string, bool) {
	if len(v) >= 3 && strings.HasPrefix(v, "%") && strings.HasSuffix(v, "%") {
		name := strings.Trim(v[1:len(v)-1], "@")
		if _, ok := g.vars[name]; ok {
			return name, true
		}
	}
	return "", false
}

func looksLikeCConst(s string) bool {
	if s == "" {
		return false
	}
	hasLetter := false
	for i, r := range s {
		switch {
		case r >= 'A' && r <= 'Z':
			hasLetter = true
		case r == '_':
		case r >= '0' && r <= '9':
			if i == 0 {
				return false
			}
		default:
			return false
		}
	}
	return hasLetter
}

func (g *Generator) cArgs(args []string) string {
	if len(args) == 0 {
		return ""
	}
	out := make([]string, len(args))
	for i, a := range args {
		out[i] = g.cArg(a)
	}
	return strings.Join(out, ", ")
}

func (g *Generator) cProcArgs(args []string) string {
	if len(args) == 0 {
		return ""
	}
	out := make([]string, len(args))
	for i, a := range args {
		out[i] = "(uintptr_t)(" + g.cArg(a) + ")"
	}
	return strings.Join(out, ", ")
}

func inferType(v string) ValType {
	v = strings.TrimSpace(v)
	if isBoolLit(v) {
		return TypeBool
	}
	if isNumber(v) {
		return TypeFloat
	}
	return TypeString
}

var numRe = regexp.MustCompile(`^-?[0-9]+(\.[0-9]+)?$`)

func isNumber(s string) bool { return numRe.MatchString(strings.TrimSpace(s)) }
func isBoolLit(s string) bool {
	l := strings.ToLower(strings.TrimSpace(s))
	return l == "true" || l == "false"
}

func splitKV(args []string) (string, string) {
	if len(args) == 0 {
		return "", ""
	}
	first := args[0]
	idx := strings.Index(first, "=")
	if idx < 0 {
		return strings.TrimSpace(first), strings.TrimSpace(strings.Join(args[1:], ","))
	}
	name := strings.TrimSpace(first[:idx])
	value := strings.TrimSpace(first[idx+1:])
	if len(args) > 1 {
		if value != "" {
			value += ","
		}
		value += strings.Join(args[1:], ",")
	}
	return name, strings.TrimSpace(value)
}

func splitMaybeAssign(args []string) (string, string) {
	if len(args) == 0 {
		return "", ""
	}
	if strings.Contains(args[0], "=") {
		return splitKV(args)
	}
	return "", strings.Join(args, ",")
}

func splitCall(s string) (string, []string) {
	parts := splitCSV(s)
	if len(parts) == 0 {
		return "", nil
	}
	return strings.TrimSpace(parts[0]), parts[1:]
}

func splitCSV(s string) []string {
	parts := strings.Split(s, ",")
	out := make([]string, 0, len(parts))
	for _, p := range parts {
		p = strings.TrimSpace(p)
		if p != "" {
			out = append(out, p)
		}
	}
	return out
}

func joinPayload(args []string) string {
	if len(args) == 0 {
		return ""
	}
	if strings.EqualFold(args[0], "INFO") || strings.EqualFold(args[0], "WARN") || strings.EqualFold(args[0], "ERROR") {
		return strings.Join(args[1:], ",")
	}
	if strings.HasPrefix(strings.ToUpper(args[0]), "INFO,") {
		args[0] = args[0][5:]
	}
	return strings.Join(args, ",")
}

func cString(s string) string { return "\"" + cEscape(strings.Trim(s, "\"")) + "\"" }
func cEscape(s string) string {
	s = strings.ReplaceAll(s, "\\", "\\\\")
	s = strings.ReplaceAll(s, "\"", "\\\"")
	s = strings.ReplaceAll(s, "\n", "\\n")
	return s
}

func findLLVMPath() string {
	paths := []string{}
	for _, name := range []string{"LLVM_BIN", "LLVM_PATH", "LLVM_HOME"} {
		if value := strings.TrimSpace(os.Getenv(name)); value != "" {
			paths = append(paths, value, filepath.Join(value, "bin"))
		}
	}
	paths = append(paths,
		`C:\Program Files\LLVM\bin`,
		`C:\LLVM\bin`,
	)
	for _, p := range paths {
		if _, err := os.Stat(filepath.Join(p, "clang.exe")); err == nil {
			return p
		}
	}
	if clang, err := exec.LookPath("clang"); err == nil {
		return filepath.Dir(clang)
	}
	return ""
}

func copyLCLRuntime(outDir string) error {
	srcDLL := filepath.Join(gsRuntimeRoot(), "lcl", "out", "gs_lcl_runtime.dll")
	srcH := filepath.Join(gsRuntimeRoot(), "lcl", "gs_lcl_runtime.h")
	if outDir == "" || outDir == "." {
		outDir = "."
	}
	for _, src := range []string{srcDLL, srcH} {
		if _, err := os.Stat(src); err != nil {
			continue
		}
		data, err := os.ReadFile(src)
		if err != nil {
			return err
		}
		if err := os.WriteFile(filepath.Join(outDir, filepath.Base(src)), data, 0644); err != nil {
			return err
		}
	}
	return nil
}

func gsRuntimeRoot() string {
	root, err := gsRootDir()
	if err != nil {
		return filepath.Join("gs", "runtime")
	}
	return filepath.Join(root, "runtime")
}

func buildMsvcEnv() []string {
	include, lib := detectMsvcPaths()
	var env []string
	if include != "" {
		env = append(env, "INCLUDE="+include)
	}
	if lib != "" {
		env = append(env, "LIB="+lib)
	}
	return env
}

func detectMsvcPaths() (string, string) {
	sdkRoot := `C:\Program Files (x86)\Windows Kits\10`
	sdkVer := latestSubdir(filepath.Join(sdkRoot, "Lib"))
	if sdkVer == "" {
		return "", ""
	}
	vsRoot := firstExisting(
		`C:\Program Files\Microsoft Visual Studio`,
		`C:\Program Files (x86)\Microsoft Visual Studio`,
	)
	if vsRoot == "" {
		return "", ""
	}
	msvcRoot := firstMsvcToolset(vsRoot)
	if msvcRoot == "" {
		return "", ""
	}

	arch := "x64"
	include := strings.Join([]string{
		filepath.Join(sdkRoot, "Include", sdkVer, "um"),
		filepath.Join(sdkRoot, "Include", sdkVer, "shared"),
		filepath.Join(sdkRoot, "Include", sdkVer, "ucrt"),
		filepath.Join(sdkRoot, "Include", sdkVer, "winrt"),
		filepath.Join(msvcRoot, "include"),
	}, ";")
	lib := strings.Join([]string{
		filepath.Join(sdkRoot, "Lib", sdkVer, "um", arch),
		filepath.Join(sdkRoot, "Lib", sdkVer, "ucrt", arch),
		filepath.Join(msvcRoot, "lib", arch),
	}, ";")
	return include, lib
}

func latestSubdir(root string) string {
	entries, err := os.ReadDir(root)
	if err != nil {
		return ""
	}
	var best string
	for _, e := range entries {
		if e.IsDir() && (best == "" || e.Name() > best) {
			best = e.Name()
		}
	}
	return best
}

func firstExisting(paths ...string) string {
	for _, p := range paths {
		if st, err := os.Stat(p); err == nil && st.IsDir() {
			return p
		}
	}
	return ""
}

func firstMsvcToolset(vsRoot string) string {
	entries, err := os.ReadDir(vsRoot)
	if err != nil {
		return ""
	}
	for _, edition := range entries {
		base := filepath.Join(vsRoot, edition.Name(), "VC", "Tools", "MSVC")
		ver := latestSubdir(base)
		if ver != "" {
			return filepath.Join(base, ver)
		}
	}
	return ""
}
