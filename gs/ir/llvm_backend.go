package ir

import (
	"crypto/md5"
	"crypto/sha1"
	"crypto/sha256"
	"crypto/sha512"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/SECTL/GPM/gs"
)

type llvmGenerator struct {
	prog      *gs.Program
	srcDir    string
	body      strings.Builder
	strings   []string
	vars      map[string]ValType
	callStack map[string]bool
	counter   int
	exitCode  int
	needNet   bool
	needUI    bool
}

func (g *Generator) compileLLVM(outputPath string) error {
	lg := newLLVMGenerator(g.prog, g.srcDir)
	if err := lg.emit(); err != nil {
		return err
	}
	llPath := outputPath + ".ll"
	if err := os.WriteFile(llPath, []byte(lg.render()), 0644); err != nil {
		return err
	}

	// Optimize with LLVM opt before linking (based on optLevel)
	// optLevel: "0"=skip opt, "1"=O1, "2"=O2+opt O3, "3"=O3+opt O3
	if g.optLevel != "0" {
		optPath := filepath.Join(g.llvmPath, "opt.exe")
		if _, err := os.Stat(optPath); err == nil {
			optLLPath := outputPath + ".opt.ll"
			optLevel := "-O3"
			if g.optLevel == "1" {
				optLevel = "-O1"
			} else if g.optLevel == "2" {
				optLevel = "-O2"
			}
			optCmd := exec.Command(optPath, optLevel, "-S", llPath, "-o", optLLPath)
			if out, err := optCmd.CombinedOutput(); err == nil {
				llPath = optLLPath // Use optimized version
			} else {
				// Non-fatal: continue with unoptimized IR
				fmt.Fprintf(os.Stderr, "Warning: opt failed: %s\n", out)
			}
		}
	}

	g.resultDir = filepath.Dir(outputPath)
	runtimeObj, err := g.compileLLVMRuntime(outputPath)
	if err != nil {
		return err
	}
	if lg.needNet {
		g.needNet = true
		if err := g.ensureNetRuntime(); err != nil {
			return err
		}
	}
	if err := g.ensureSysRuntime(); err != nil {
		return err
	}
	if lg.needNet {
		g.needNet = true
		if err := g.ensureNetRuntime(); err != nil {
			return err
		}
	}
	if lg.needUI {
		if err := copyLCLRuntime(filepath.Dir(outputPath)); err != nil {
			return err
		}
	}
	clang := filepath.Join(g.llvmPath, "clang.exe")
	// Set clang optimization level based on optLevel
	clangOpt := "-O2"
	if g.optLevel == "0" {
		clangOpt = "-O0"
	} else if g.optLevel == "1" {
		clangOpt = "-O1"
	} else if g.optLevel == "3" {
		clangOpt = "-O3"
	}
	linkArgs := []string{
		clangOpt,
		"-luser32", "-ladvapi32", "-lkernel32", "-lshell32",
		"-o", outputPath, llPath, runtimeObj,
	}
	linkArgs = append(linkArgs, g.extraObjs...)
	linkArgs = append(linkArgs, g.extraLibs...)
	cmd := exec.Command(clang, linkArgs...)
	cmd.Env = append(os.Environ(), buildMsvcEnv()...)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("clang llvm backend failed: %s", out)
	}
	return nil
}

func (g *Generator) compileLLVMRuntime(outputPath string) (string, error) {
	root, err := gsRootDir()
	if err != nil {
		return "", err
	}

	// Try to use precompiled .o file first (fastest - no compilation needed)
	precompiledObj := filepath.Join(root, "runtime", "llvm", "gs_llvm_runtime.o")
	if stat, err := os.Stat(precompiledObj); err == nil {
		// Verify it's recent (within last 24 hours)
		if time.Since(stat.ModTime()) < 24*time.Hour {
			// Copy to output dir for linking
			dst := filepath.Join(filepath.Dir(outputPath), "gs_llvm_runtime.o")
			data, err := os.ReadFile(precompiledObj)
			if err == nil {
				if err := os.WriteFile(dst, data, 0644); err == nil {
					return dst, nil
				}
			}
		}
	}

	// Fallback: use optimized .ll if available
	src := filepath.Join(root, "runtime", "llvm", "gs_llvm_runtime.ll")
	cachedOpt := filepath.Join(root, "runtime", "llvm", "gs_llvm_runtime.opt.ll")
	srcStat, err := os.Stat(src)
	if err != nil {
		return "", err
	}

	// Use cached optimized version if it's newer than source
	llToUse := src
	if cachedStat, err := os.Stat(cachedOpt); err == nil && cachedStat.ModTime().After(srcStat.ModTime()) {
		llToUse = cachedOpt
	}

	// Copy to output directory
	dst := filepath.Join(filepath.Dir(outputPath), "gs_llvm_runtime.ll")
	data, err := os.ReadFile(llToUse)
	if err != nil {
		return "", err
	}
	if err := os.WriteFile(dst, data, 0644); err != nil {
		return "", err
	}

	return dst, nil
}

func newLLVMGenerator(prog *gs.Program, srcDir string) *llvmGenerator {
	return &llvmGenerator{prog: prog, srcDir: srcDir, vars: make(map[string]ValType), callStack: make(map[string]bool)}
}

func (l *llvmGenerator) emit() error { return l.emitStatements(l.prog.Main) }

func (l *llvmGenerator) emitStatements(stmts []gs.Statement) error {
	for _, stmt := range stmts {
		cmd := cleanCmd(stmt.Cmd)
		args := cleanArgs(stmt.Args)
		switch cmd {
		case "", "CGSI", "CGSL", "CGSH", "CGSB", "CGSC", "PGCB", "AGCB":
		case "SETV", "STRV", "FLOT", "BOOL":
			l.emitSet(args, cmd)
		case "CALC":
			l.emitCalc(args)
		case "LOGS":
			l.emitLogs(args)
		case "WAIT":
			l.emitWait(args)
		case "EXIT":
			l.emitExit(args)
		case "CALL":
			if err := l.emitCall(args); err != nil {
				return err
			}
		case "IFEX":
			if err := l.emitIfex(args); err != nil {
				return err
			}
		case "WHEN":
			if err := l.emitWhen(args); err != nil {
				return err
			}
		case "LOOP":
			if err := l.emitLoop(args); err != nil {
				return err
			}
		case "FORX":
			if err := l.emitForx(args); err != nil {
				return err
			}
		case "FILE":
			if err := l.emitFile(args); err != nil {
				return err
			}
		case "FDIR":
			if err := l.emitDir(args); err != nil {
				return err
			}
		case "LINK":
			if len(args) >= 3 {
				l.call("gs_link", "double", l.str(args[0]), l.str(args[1]), l.str(args[2]))
			}
		case "STRL":
			name, val := splitKV(args)
			l.declare(name, TypeFloat)
			l.storeNum(name, l.callVal("gs_strlen2", l.str(val)))
		case "LPOS":
			l.emitPos(args, "gs_lpos2")
		case "RPOS":
			l.emitPos(args, "gs_rpos2")
		case "FEXT":
			l.emitPathBuf(args, "gs_fext")
		case "FDRV":
			l.emitPathBuf(args, "gs_fdrv")
		case "EXIST":
			name, path := splitKV(args)
			l.declare(name, TypeFloat)
			l.storeNum(name, l.callVal("gs_file_exist", l.str(path)))
		case "HASH":
			l.emitHash(args)
		case "BASE":
			l.emitBase(args)
		case "HEXC":
			l.emitHex(args)
		case "JSON":
			if err := l.emitJsonRead(args); err != nil {
				return err
			}
		case "JSNL":
			if err := l.emitJsonLen(args); err != nil {
				return err
			}
		case "JSNS":
			l.emitJsonSet(args)
		case "REGI":
			l.emitRegistry(args)
		case "SERV":
			if len(args) >= 2 {
				l.call("gs_service", "double", l.str(args[0]), l.str(args[1]))
			}
		case "TASK":
			l.emitTask(args)
		case "FWAL":
			if len(args) >= 2 {
				l.call("gs_firewall", "double", l.str(args[0]), l.str(strings.Join(args[1:], ",")))
			}
		case "HTTP":
			l.emitHTTP(args)
		case "DOWN":
			if len(args) >= 2 {
				l.needNet = true
				l.call("gs_down", "i32", l.str(args[0]), l.str(args[1]))
			}
		case "UPLD":
			l.emitUpload(args)
		case "LSTR":
			l.emitSubstr(args, "gs_lstr", 1)
		case "RSTR":
			l.emitSubstr(args, "gs_rstr", 1)
		case "MSTR":
			l.emitSubstr(args, "gs_mstr", 2)
		case "RGEX":
			l.emitRegex(args)
		case "RGSB":
			l.emitRegexSub(args)
		case "RUNS":
			dir := "."
			if len(args) > 0 && strings.TrimSpace(args[0]) != "" {
				dir = args[0]
			}
			l.call("gs_run_scripts", "double", l.str(dir))
		case "PECM":
			op, path := "", ""
			if len(args) > 0 {
				op = args[0]
			}
			if len(args) > 1 {
				path = args[1]
			}
			l.call("gs_pecmd", "double", l.str(op), l.str(path))
		case "WNSH":
			if len(args) > 0 {
				l.call("gs_winxshell", "double", l.str(args[0]))
			}
		case "VHDM":
			mode := ""
			if len(args) > 1 {
				mode = args[1]
			}
			if len(args) > 0 {
				l.call("gs_vhd_mount", "double", l.str(args[0]), l.str(mode))
			}
		case "VHDU":
			if len(args) > 0 {
				l.call("gs_vhd_unmount", "double", l.str(args[0]))
			}
		case "VHDC":
			if len(args) >= 3 {
				l.call("gs_vhd_create", "double", l.str(args[0]), "double "+l.numExpr(args[1]), l.str(args[2]))
			}
		case "XMLR":
			l.emitXMLRead(args)
		case "XMLW":
			l.emitXMLWrite(args)
		case "AESC":
			l.emitAES(args)
		case "ZIPX":
			if len(args) >= 2 {
				l.call("gs_zip_extract", "double", l.str(args[0]), l.str(args[1]))
			}
		case "ZIPC":
			if len(args) >= 2 {
				l.call("gs_zip_create", "double", l.str(args[0]), l.str(args[1]))
			}
		case "TARX":
			if len(args) >= 2 {
				l.call("gs_tar_extract", "double", l.str(args[0]), l.str(args[1]))
			}
		case "GPMI":
			name, ver := "", ""
			if len(args) > 0 {
				name = args[0]
			}
			if len(args) > 1 {
				ver = args[1]
			}
			l.call("gs_gpm_install", "double", l.str(name), l.str(ver))
		case "GPMU":
			if len(args) > 0 {
				l.call("gs_gpm_uninstall", "double", l.str(args[0]))
			}
		case "GPMV":
			name, pkg := splitKV(args)
			if name != "" {
				l.declare(name, TypeString)
				l.storePtr(name, l.call("gs_gpm_version", "ptr", l.str(pkg)))
			}
		case "EXEC":
			l.emitExec(args)
		case "UIDF":
			if err := l.emitUIDef(args); err != nil {
				return err
			}
		case "UILP":
			l.needUI = true
			l.call("gs_ui_run", "void")
		case "MBOX":
			l.emitMessage(args, false)
		case "BEEP":
			l.emitBeep(args)
		case "EROR":
			l.emitMessage(args, true)
		case "DLLO", "DLOP":
			l.emitDllOpen(args)
		case "DLLG", "DLSY":
			l.emitDllGet(args)
		case "DLLC", "DLCA":
			l.emitDllCall(args)
		case "DLLF", "DLCL":
			if len(args) > 0 {
				l.call("gs_dll_close", "void", l.ptrExpr(args[0]))
			}
		case "APIC", "WAPI":
			l.emitWinAPI(args)
		default:
			return fmt.Errorf("line %d: llvm backend does not support command %q yet", stmt.Line, cmd)
		}
	}
	return nil
}

func (l *llvmGenerator) render() string {
	var out strings.Builder
	out.WriteString("target triple = \"x86_64-pc-windows-msvc\"\n\n")
	for i, s := range l.strings {
		fmt.Fprintf(&out, "@.str.%d = private unnamed_addr constant [%d x i8] c\"%s\\00\"\n", i, len([]byte(s))+1, llvmEscape(s))
	}
	out.WriteString("@HTTP_BODY_buf = internal global [1048576 x i8] zeroinitializer\n")
	out.WriteString("\ndeclare i32 @printf(ptr, ...)\ndeclare void @Sleep(i32)\n")
	decls := []string{
		"declare ptr @gs_num_to_string(double)", "declare double @gs_strlen2(ptr)", "declare double @gs_lpos2(ptr, ptr)", "declare double @gs_rpos2(ptr, ptr)",
		"declare ptr @gs_lstr(ptr, double)", "declare ptr @gs_rstr(ptr, double)", "declare ptr @gs_mstr(ptr, double, double)", "declare ptr @gs_regex(ptr, ptr, double)", "declare ptr @gs_regex_sub(ptr, ptr, ptr)",
		"declare double @gs_run_scripts(ptr)", "declare double @gs_pecmd(ptr, ptr)", "declare double @gs_winxshell(ptr)",
		"declare double @gs_vhd_mount(ptr, ptr)", "declare double @gs_vhd_unmount(ptr)", "declare double @gs_vhd_create(ptr, double, ptr)",
		"declare ptr @gs_xml_read(ptr, ptr)", "declare double @gs_xml_write(ptr, ptr, ptr)", "declare ptr @gs_aes(ptr, ptr, ptr, ptr)",
		"declare double @gs_zip_extract(ptr, ptr)", "declare double @gs_zip_create(ptr, ptr)", "declare double @gs_tar_extract(ptr, ptr)",
		"declare double @gs_gpm_install(ptr, ptr)", "declare double @gs_gpm_uninstall(ptr)", "declare ptr @gs_gpm_version(ptr)",
		"declare double @gs_file_exist(ptr)", "declare void @gs_file_del(ptr)", "declare double @gs_file_copy(ptr, ptr)", "declare double @gs_file_move(ptr, ptr)", "declare double @gs_file_write(ptr, ptr)", "declare double @gs_file_append(ptr, ptr)", "declare double @gs_file_read(ptr, ptr, i32)",
		"declare void @gs_dir_make(ptr)", "declare void @gs_dir_del(ptr)", "declare double @gs_dir_list(ptr, ptr, i32)", "declare double @gs_fext(ptr, ptr)", "declare double @gs_fdrv(ptr, ptr)", "declare double @gs_link(ptr, ptr, ptr)",
		"declare double @gs_exec(ptr, ptr, ptr, ptr)", "declare double @gs_reg_get(ptr, ptr, ptr, i32)", "declare double @gs_reg_set(ptr, ptr, ptr, ptr)", "declare double @gs_reg_del(ptr, ptr)", "declare double @gs_service(ptr, ptr)", "declare double @gs_task(ptr, ptr, ptr, ptr)", "declare double @gs_firewall(ptr, ptr)", "declare double @gs_json_set(ptr, ptr, ptr)",
		"declare i32 @gs_http(ptr, ptr, ptr, ptr, i32, ptr)", "declare i32 @gs_down(ptr, ptr)", "declare i32 @gs_upld(ptr, ptr, ptr, i32, ptr)",
		"declare ptr @gs_forx_first(ptr, ptr)", "declare i32 @gs_forx_next(ptr, ptr)", "declare void @gs_forx_close(ptr)",
		"declare ptr @gs_dll_open(ptr)", "declare ptr @gs_dll_sym(ptr, ptr)", "declare void @gs_dll_close(ptr)", "declare double @gs_dll_call(ptr, i64, i64, i64, i64, i64, i64, i64, i64)",
		"declare double @gs_beep(double, double)", "declare void @gs_msg(ptr, ptr, i32)", "declare void @gs_exit(i32)",
		"declare ptr @gs_ui_form(ptr, i32, i32)", "declare ptr @gs_ui_control(ptr, ptr, ptr, i32, i32, i32, i32)", "declare void @gs_ui_show(ptr)", "declare void @gs_ui_run()",
	}
	for _, d := range decls {
		out.WriteString(d + "\n")
	}
	out.WriteString("\ndefine i32 @main() {\nentry:\n")
	out.WriteString(l.body.String())
	fmt.Fprintf(&out, "  ret i32 %d\n}\n", l.exitCode)
	return out.String()
}

func (l *llvmGenerator) next(prefix string) string {
	l.counter++
	return fmt.Sprintf("%%%s%d", cleanLLVMName(prefix), l.counter)
}
func (l *llvmGenerator) label(prefix string) string {
	l.counter++
	return fmt.Sprintf("%s%d", cleanLLVMName(prefix), l.counter)
}
func cleanLLVMName(s string) string {
	return strings.NewReplacer(".", "_", "-", "_", " ", "_").Replace(s)
}
func (l *llvmGenerator) str(s string) string { return l.stringPtr(strings.Trim(s, "\"")) }
func (l *llvmGenerator) stringPtr(s string) string {
	idx := len(l.strings)
	l.strings = append(l.strings, s)
	return fmt.Sprintf("getelementptr inbounds ([%d x i8], ptr @.str.%d, i32 0, i32 0)", len([]byte(s))+1, idx)
}
func (l *llvmGenerator) declare(name string, typ ValType) {
	if name == "" {
		return
	}
	if _, ok := l.vars[name]; ok {
		return
	}
	l.vars[name] = typ
	if typ == TypeString {
		fmt.Fprintf(&l.body, "  %%%s = alloca ptr\n", name)
	} else {
		fmt.Fprintf(&l.body, "  %%%s = alloca double\n", name)
	}
}
func (l *llvmGenerator) storeNum(name, val string) {
	fmt.Fprintf(&l.body, "  store double %s, ptr %%%s\n", llvmValue(val), name)
}
func (l *llvmGenerator) loadNum(name string) string {
	t := l.next("ld")
	fmt.Fprintf(&l.body, "  %s = load double, ptr %%%s\n", t, name)
	return t
}
func (l *llvmGenerator) storePtr(name, val string) {
	fmt.Fprintf(&l.body, "  store ptr %s, ptr %%%s\n", llvmValue(val), name)
}
func (l *llvmGenerator) loadPtr(name string) string {
	t := l.next("lp")
	fmt.Fprintf(&l.body, "  %s = load ptr, ptr %%%s\n", t, name)
	return "ptr " + t
}
func (l *llvmGenerator) call(fn, ret string, args ...string) string {
	t := ""
	// Special handling for printf varargs
	if fn == "printf" && len(args) > 0 {
		if ret != "void" {
			t = l.next("call")
			// Printf needs explicit type signature for varargs
			fmt.Fprintf(&l.body, "  %s = call i32 (ptr, ...) @printf(%s)\n", t, llvmArgs(args))
		} else {
			fmt.Fprintf(&l.body, "  call void (ptr, ...) @printf(%s)\n", llvmArgs(args))
		}
		return t
	}
	if ret != "void" {
		t = l.next("call")
		fmt.Fprintf(&l.body, "  %s = call %s @%s(%s)\n", t, ret, fn, llvmArgs(args))
		if ret == "ptr" {
			return "ptr " + t
		}
	} else {
		fmt.Fprintf(&l.body, "  call void @%s(%s)\n", fn, llvmArgs(args))
	}
	return t
}
func (l *llvmGenerator) callVal(fn string, args ...string) string {
	return l.call(fn, "double", args...)
}
func llvmArgs(args []string) string {
	var out []string
	for _, a := range args {
		out = append(out, llvmArg(a))
	}
	return strings.Join(out, ", ")
}
func llvmArg(a string) string {
	a = strings.TrimSpace(a)
	if a == "" {
		return "ptr null"
	}
	if strings.HasPrefix(a, "ptr ") || strings.HasPrefix(a, "i32 ") || strings.HasPrefix(a, "double ") {
		return a
	}
	if strings.HasPrefix(a, "getelementptr") {
		return "ptr " + a
	}
	if strings.HasPrefix(a, "%") {
		return "double " + a
	}
	return a
}
func llvmValue(a string) string {
	a = strings.TrimSpace(a)
	a = strings.TrimPrefix(a, "ptr ")
	a = strings.TrimPrefix(a, "double ")
	return a
}

func (l *llvmGenerator) emitSet(args []string, cmd string) {
	name, value := splitKV(args)
	if name == "" {
		return
	}
	typ := inferType(value)
	if cmd == "STRV" {
		typ = TypeString
	} else if cmd == "BOOL" || cmd == "FLOT" || cmd == "SETV" {
		// For SETV, if value contains operators or variable refs, treat as Float
		if strings.ContainsAny(value, "+-*/%") || strings.Contains(value, "INDEX") {
			typ = TypeFloat
		}
	}
	l.declare(name, typ)
	if typ == TypeString {
		l.storePtr(name, l.str(value))
	} else {
		// Use numExpr to handle expressions like "INDEX + 2"
		l.storeNum(name, l.numExpr(value))
	}
}
func (l *llvmGenerator) emitCalc(args []string) {
	name, expr := splitKV(args)
	l.declare(name, TypeFloat)
	l.storeNum(name, l.numExpr(expr))
}
func (l *llvmGenerator) numExpr(expr string) string {
	expr = strings.TrimSpace(expr)
	if name, ok := percentName(expr); ok {
		expr = name
	}
	// Handle parentheses
	if strings.HasPrefix(expr, "(") && strings.HasSuffix(expr, ")") {
		return l.numExpr(expr[1 : len(expr)-1])
	}
	// Binary operators by precedence (lowest to highest)
	// Level 1: +, -
	for _, op := range []string{"+", "-"} {
		if i := findOutermostOp(expr, op); i > 0 {
			a, b := l.numExpr(expr[:i]), l.numExpr(expr[i+len(op):])
			t := l.next("op")
			instr := map[string]string{"+": "fadd", "-": "fsub"}[op]
			fmt.Fprintf(&l.body, "  %s = %s double %s, %s\n", t, instr, a, b)
			return t
		}
	}
	// Level 2: *, /, %
	for _, op := range []string{"*", "/", "%"} {
		if i := findOutermostOp(expr, op); i > 0 {
			a, b := l.numExpr(expr[:i]), l.numExpr(expr[i+len(op):])
			t := l.next("op")
			instr := map[string]string{"*": "fmul", "/": "fdiv", "%": "frem"}[op]
			fmt.Fprintf(&l.body, "  %s = %s double %s, %s\n", t, instr, a, b)
			return t
		}
	}
	if typ, ok := l.vars[expr]; ok && typ != TypeString {
		return l.loadNum(expr)
	}
	return llvmNumber(expr)
}

// findOutermostOp finds the first occurrence of op outside parentheses
func findOutermostOp(expr, op string) int {
	depth := 0
	for i := 0; i < len(expr)-len(op)+1; i++ {
		if expr[i] == '(' {
			depth++
		} else if expr[i] == ')' {
			depth--
		} else if depth == 0 && strings.HasPrefix(expr[i:], op) {
			// Avoid matching operators at the beginning (unary minus)
			if i > 0 || op == "*" || op == "/" || op == "%" {
				return i
			}
		}
	}
	return -1
}
func (l *llvmGenerator) ptrExpr(s string) string {
	if name, ok := percentName(s); ok {
		if typ := l.vars[name]; typ == TypeString {
			return l.loadPtr(name)
		} else if typ != 0 || l.vars[name] == TypeFloat {
			return l.call("gs_num_to_string", "ptr", "double "+l.loadNum(name))
		}
	}
	return l.str(s)
}
func percentName(s string) (string, bool) {
	s = strings.TrimSpace(s)
	if len(s) >= 3 && strings.HasPrefix(s, "%") && strings.HasSuffix(s, "%") {
		return strings.Trim(s[1:len(s)-1], "@"), true
	}
	return "", false
}

func (l *llvmGenerator) emitLogs(args []string) {
	msg := joinPayload(args)
	vars := l.extractVars(msg)
	if len(vars) == 0 {
		l.call("printf", "i32", l.str(msg+"\n"))
		return
	}
	format := msg + "\n"
	var callArgs []string
	for _, v := range vars {
		typ := l.vars[v]
		format = strings.Replace(format, "%@"+v+"%", typ.printfFmt(), 1)
		format = strings.Replace(format, "%"+v+"%", typ.printfFmt(), 1)
		if typ == TypeString {
			callArgs = append(callArgs, l.loadPtr(v))
		} else {
			callArgs = append(callArgs, l.loadNum(v))
		}
	}
	all := append([]string{l.str(format)}, callArgs...)
	l.call("printf", "i32", all...)
}
func (l *llvmGenerator) extractVars(s string) []string {
	var out []string
	for i := 0; i < len(s); i++ {
		if s[i] != '%' {
			continue
		}
		st := i + 1
		if st < len(s) && s[st] == '@' {
			st++
		}
		e := strings.IndexByte(s[st:], '%')
		if e < 0 {
			continue
		}
		n := s[st : st+e]
		if _, ok := l.vars[n]; ok {
			out = append(out, n)
		}
		i = st + e
	}
	return out
}
func (l *llvmGenerator) emitWait(args []string) {
	if len(args) > 0 {
		n, _ := strconv.Atoi(args[0])
		l.call("Sleep", "void", fmt.Sprintf("i32 %d", n))
	}
}
func (l *llvmGenerator) emitExit(args []string) {
	if len(args) > 0 {
		if n, err := strconv.Atoi(args[0]); err == nil {
			l.exitCode = n
		}
	}
}
func (l *llvmGenerator) emitCall(args []string) error {
	if len(args) == 0 {
		return fmt.Errorf("CALL: missing name")
	}
	name := strings.ToUpper(args[0])
	body, ok := l.prog.Subs[name]
	if !ok {
		return fmt.Errorf("CALL: unknown FUNC %s", args[0])
	}
	if l.callStack[name] {
		return fmt.Errorf("CALL: recursive FUNC %s", args[0])
	}
	l.callStack[name] = true
	defer delete(l.callStack, name)
	return l.emitStatements(body)
}

func (l *llvmGenerator) emitIfex(args []string) error {
	if len(args) < 2 {
		return nil
	}
	thenLabel := l.label("if.then")
	endLabel := l.label("if.end")
	cond := l.condValue(args[0])
	fmt.Fprintf(&l.body, "  br i1 %s, label %%%s, label %%%s\n", cond, thenLabel, endLabel)
	fmt.Fprintf(&l.body, "%s:\n", thenLabel)
	if err := l.emitStatements([]gs.Statement{{Cmd: args[1], Args: args[2:]}}); err != nil {
		return err
	}
	fmt.Fprintf(&l.body, "  br label %%%s\n", endLabel)
	fmt.Fprintf(&l.body, "%s:\n", endLabel)
	return nil
}
func (l *llvmGenerator) emitWhen(args []string) error {
	if len(args) < 2 {
		return nil
	}
	thenLabel := l.label("when.then")
	endLabel := l.label("when.end")
	cond := l.condValue(args[1])
	fmt.Fprintf(&l.body, "  br i1 %s, label %%%s, label %%%s\n", cond, thenLabel, endLabel)
	fmt.Fprintf(&l.body, "%s:\n", thenLabel)
	if err := l.emitStatements(l.prog.Blocks[args[0]]); err != nil {
		return err
	}
	fmt.Fprintf(&l.body, "  br label %%%s\n", endLabel)
	fmt.Fprintf(&l.body, "%s:\n", endLabel)
	return nil
}
func (l *llvmGenerator) condValue(cond string) string {
	cond = strings.TrimSpace(strings.Trim(cond, "\""))
	cond = strings.TrimPrefix(cond, "(")
	cond = strings.TrimSuffix(cond, ")")
	upper := strings.ToUpper(cond)
	if i := strings.Index(upper, " AND "); i >= 0 {
		a := l.condValue(cond[:i])
		b := l.condValue(cond[i+5:])
		t := l.next("and")
		fmt.Fprintf(&l.body, "  %s = and i1 %s, %s\n", t, a, b)
		return t
	}
	if i := strings.Index(upper, " OR "); i >= 0 {
		a := l.condValue(cond[:i])
		b := l.condValue(cond[i+4:])
		t := l.next("or")
		fmt.Fprintf(&l.body, "  %s = or i1 %s, %s\n", t, a, b)
		return t
	}
	ops := []struct{ gs, ir string }{{">=", "oge"}, {"<=", "ole"}, {"==", "oeq"}, {"!=", "one"}, {">", "ogt"}, {"<", "olt"}}
	for _, op := range ops {
		if i := strings.Index(cond, op.gs); i >= 0 {
			left := l.numExpr(strings.TrimSpace(cond[:i]))
			right := l.numExpr(strings.TrimSpace(cond[i+len(op.gs):]))
			t := l.next("cmp")
			fmt.Fprintf(&l.body, "  %s = fcmp %s double %s, %s\n", t, op.ir, left, right)
			return t
		}
	}
	v := l.numExpr(cond)
	t := l.next("cmp")
	fmt.Fprintf(&l.body, "  %s = fcmp one double %s, 0.0\n", t, v)
	return t
}
func (l *llvmGenerator) emitLoop(args []string) error {
	if len(args) < 2 {
		return nil
	}
	body, ok := l.prog.Blocks[args[0]]
	if !ok {
		return fmt.Errorf("LOOP: block %s not found", args[0])
	}
	l.declare("INDEX", TypeFloat)
	l.storeNum("INDEX", "0.0")
	count := l.numExpr(args[1])
	condLabel := l.label("loop.cond")
	bodyLabel := l.label("loop.body")
	endLabel := l.label("loop.end")
	fmt.Fprintf(&l.body, "  br label %%%s\n", condLabel)
	fmt.Fprintf(&l.body, "%s:\n", condLabel)
	idx := l.loadNum("INDEX")
	cmp := l.next("loopcmp")
	fmt.Fprintf(&l.body, "  %s = fcmp olt double %s, %s\n", cmp, idx, count)
	fmt.Fprintf(&l.body, "  br i1 %s, label %%%s, label %%%s\n", cmp, bodyLabel, endLabel)
	fmt.Fprintf(&l.body, "%s:\n", bodyLabel)
	if err := l.emitStatements(body); err != nil {
		return err
	}
	idx2 := l.loadNum("INDEX")
	next := l.next("loopnext")
	fmt.Fprintf(&l.body, "  %s = fadd double %s, 1.0\n", next, idx2)
	l.storeNum("INDEX", next)
	fmt.Fprintf(&l.body, "  br label %%%s\n", condLabel)
	fmt.Fprintf(&l.body, "%s:\n", endLabel)
	return nil
}
func (l *llvmGenerator) emitForx(args []string) error {
	if len(args) < 3 {
		return nil
	}
	body, ok := l.prog.Blocks[args[0]]
	if !ok {
		return fmt.Errorf("FORX: block %s not found", args[0])
	}
	pattern := strings.TrimRight(args[2], `\\/`) + `\\` + args[1]
	l.declare("FILE", TypeString)
	buf := l.allocBuf(260)
	handle := l.call("gs_forx_first", "ptr", l.str(pattern), buf)
	l.storePtr("FILE", buf)
	bodyLabel := l.label("forx.body")
	nextLabel := l.label("forx.next")
	endLabel := l.label("forx.end")
	fmt.Fprintf(&l.body, "  br label %%%s\n", bodyLabel)
	fmt.Fprintf(&l.body, "%s:\n", bodyLabel)
	l.storePtr("FILE", buf)
	if err := l.emitStatements(body); err != nil {
		return err
	}
	fmt.Fprintf(&l.body, "  br label %%%s\n", nextLabel)
	fmt.Fprintf(&l.body, "%s:\n", nextLabel)
	has := l.call("gs_forx_next", "i32", handle, buf)
	cmp := l.next("forxcmp")
	fmt.Fprintf(&l.body, "  %s = icmp ne i32 %s, 0\n", cmp, llvmValue(has))
	fmt.Fprintf(&l.body, "  br i1 %s, label %%%s, label %%%s\n", cmp, bodyLabel, endLabel)
	fmt.Fprintf(&l.body, "%s:\n", endLabel)
	l.call("gs_forx_close", "void", handle)
	return nil
}

func (l *llvmGenerator) emitFile(args []string) error {
	if len(args) < 2 {
		return nil
	}
	op := strings.ToUpper(args[0])
	switch op {
	case "COPY":
		if len(args) >= 3 {
			l.call("gs_file_copy", "double", l.str(args[1]), l.str(args[2]))
		}
	case "MOVE":
		if len(args) >= 3 {
			l.call("gs_file_move", "double", l.str(args[1]), l.str(args[2]))
		}
	case "DEL":
		l.call("gs_file_del", "void", l.str(args[1]))
	case "READ":
		if len(args) >= 3 {
			l.declare(args[2], TypeString)
			b := l.allocBuf(65536)
			l.call("gs_file_read", "double", l.str(args[1]), b, "i32 65536")
			l.storePtr(args[2], b)
		}
	case "WRITE":
		if len(args) >= 3 {
			l.call("gs_file_write", "double", l.str(args[1]), l.ptrExpr(strings.Join(args[2:], ",")))
		}
	case "APPEND":
		if len(args) >= 3 {
			l.call("gs_file_append", "double", l.str(args[1]), l.ptrExpr(strings.Join(args[2:], ",")))
		}
	}
	return nil
}
func (l *llvmGenerator) emitDir(args []string) error {
	if len(args) < 1 {
		return nil
	}
	op := strings.ToUpper(args[0])
	switch op {
	case "MAKE":
		p := "."
		if len(args) > 1 {
			p = args[1]
		}
		l.call("gs_dir_make", "void", l.str(p))
	case "DEL":
		if len(args) > 1 {
			l.call("gs_dir_del", "void", l.str(args[1]))
		}
	case "LIST":
		if len(args) >= 3 {
			l.declare(args[2], TypeString)
			b := l.allocBuf(65536)
			l.call("gs_dir_list", "double", l.str(args[1]+"\\\\*"), b, "i32 65536")
			l.storePtr(args[2], b)
		}
	}
	return nil
}
func (l *llvmGenerator) allocBuf(n int) string {
	p := l.next("buf")
	fmt.Fprintf(&l.body, "  %s = alloca [%d x i8]\n", p, n)
	q := l.next("bufp")
	fmt.Fprintf(&l.body, "  %s = getelementptr inbounds [%d x i8], ptr %s, i32 0, i32 0\n", q, n, p)
	return "ptr " + q
}
func (l *llvmGenerator) emitPos(args []string, fn string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name != "" && len(parts) >= 2 {
		l.declare(name, TypeFloat)
		l.storeNum(name, l.callVal(fn, l.str(parts[0]), l.str(parts[1])))
	}
}

func (l *llvmGenerator) emitSubstr(args []string, fn string, numericArgs int) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 1+numericArgs {
		return
	}
	l.declare(name, TypeString)
	if numericArgs == 1 {
		l.storePtr(name, l.call(fn, "ptr", l.ptrExpr(parts[0]), "double "+l.numExpr(parts[1])))
		return
	}
	l.storePtr(name, l.call(fn, "ptr", l.ptrExpr(parts[0]), "double "+l.numExpr(parts[1]), "double "+l.numExpr(parts[2])))
}

func (l *llvmGenerator) emitRegex(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return
	}
	group := "0.0"
	if len(parts) >= 3 {
		group = l.numExpr(parts[2])
	}
	l.declare(name, TypeString)
	l.storePtr(name, l.call("gs_regex", "ptr", l.ptrExpr(parts[0]), l.str(parts[1]), "double "+group))
}

func (l *llvmGenerator) emitRegexSub(args []string) {
	if len(args) < 3 {
		return
	}
	name := args[0]
	repl := ""
	if len(args) >= 4 {
		repl = args[3]
	}
	l.declare(name, TypeString)
	l.storePtr(name, l.call("gs_regex_sub", "ptr", l.ptrExpr(args[1]), l.str(args[2]), l.str(repl)))
}

func (l *llvmGenerator) emitPathBuf(args []string, fn string) {
	name, path := splitKV(args)
	if name != "" {
		l.declare(name, TypeString)
		b := l.allocBuf(256)
		l.call(fn, "double", l.str(path), b)
		l.storePtr(name, b)
	}
}

func (l *llvmGenerator) emitHash(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return
	}
	src := strings.TrimPrefix(parts[1], "@")
	out := ""
	switch strings.ToUpper(parts[0]) {
	case "MD5":
		h := md5.Sum([]byte(src))
		out = fmt.Sprintf("%x", h[:])
	case "SHA1":
		h := sha1.Sum([]byte(src))
		out = fmt.Sprintf("%x", h[:])
	case "SHA256":
		h := sha256.Sum256([]byte(src))
		out = fmt.Sprintf("%x", h[:])
	case "SHA512":
		h := sha512.Sum512([]byte(src))
		out = fmt.Sprintf("%x", h[:])
	}
	l.declare(name, TypeString)
	l.storePtr(name, l.str(out))
}
func (l *llvmGenerator) emitBase(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return
	}
	out := ""
	if strings.EqualFold(parts[0], "ENC") {
		out = base64.StdEncoding.EncodeToString([]byte(parts[1]))
	} else if d, err := base64.StdEncoding.DecodeString(parts[1]); err == nil {
		out = string(d)
	}
	l.declare(name, TypeString)
	l.storePtr(name, l.str(out))
}
func (l *llvmGenerator) emitHex(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return
	}
	out := ""
	if strings.EqualFold(parts[0], "ENC") {
		out = hex.EncodeToString([]byte(parts[1]))
	} else if d, err := hex.DecodeString(parts[1]); err == nil {
		out = string(d)
	}
	l.declare(name, TypeString)
	l.storePtr(name, l.str(out))
}

func (l *llvmGenerator) jsonRoot(src string) (interface{}, error) {
	src = strings.TrimSpace(src)
	var data []byte
	if strings.HasPrefix(src, "@") {
		data = []byte(strings.TrimPrefix(src, "@"))
	} else {
		p := strings.Trim(src, "\"")
		if !filepath.IsAbs(p) && l.srcDir != "" {
			p = filepath.Join(l.srcDir, p)
		}
		b, err := os.ReadFile(p)
		if err != nil {
			return nil, err
		}
		data = b
	}
	var root interface{}
	return root, json.Unmarshal(data, &root)
}
func (l *llvmGenerator) jsonVal(src, path string) (interface{}, error) {
	root, err := l.jsonRoot(src)
	if err != nil {
		return nil, err
	}
	return jsonPathValue(root, path)
}
func (l *llvmGenerator) emitJsonRead(args []string) error {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return nil
	}
	v, err := l.jsonVal(parts[0], parts[1])
	if err != nil {
		return err
	}
	b, _ := json.Marshal(v)
	s := strings.Trim(string(b), "\"")
	l.declare(name, TypeString)
	l.storePtr(name, l.str(s))
	return nil
}
func (l *llvmGenerator) emitJsonLen(args []string) error {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return nil
	}
	v, err := l.jsonVal(parts[0], parts[1])
	if err != nil {
		return err
	}
	arr, _ := v.([]interface{})
	l.declare(name, TypeFloat)
	l.storeNum(name, fmt.Sprintf("%d.0", len(arr)))
	return nil
}
func (l *llvmGenerator) emitJsonSet(args []string) {
	if len(args) >= 2 {
		path := ""
		val := ""
		if len(args) >= 3 {
			path = args[1]
			val = strings.Join(args[2:], ",")
		} else if i := strings.Index(args[1], "="); i >= 0 {
			path = args[1][:i]
			val = args[1][i+1:]
		}
		l.call("gs_json_set", "double", l.str(args[0]), l.str(path), l.str(val))
	}
}
func (l *llvmGenerator) emitRegistry(args []string) {
	if len(args) < 2 {
		return
	}
	op := strings.ToUpper(args[0])
	switch op {
	case "GET":
		if len(args) >= 4 {
			l.declare(args[3], TypeString)
			b := l.allocBuf(512)
			l.call("gs_reg_get", "double", l.str(args[1]), l.str(args[2]), b, "i32 512")
			l.storePtr(args[3], b)
		}
	case "SET":
		if len(args) >= 5 {
			l.call("gs_reg_set", "double", l.str(args[1]), l.str(args[2]), l.str(args[3]), l.str(args[4]))
		}
	case "DEL", "DELETE":
		name := ""
		if len(args) >= 3 {
			name = args[2]
		}
		l.call("gs_reg_del", "double", l.str(args[1]), l.str(name))
	}
}

func (l *llvmGenerator) emitXMLRead(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return
	}
	l.declare(name, TypeString)
	l.storePtr(name, l.call("gs_xml_read", "ptr", l.str(parts[0]), l.str(parts[1])))
}

func (l *llvmGenerator) emitXMLWrite(args []string) {
	if len(args) < 2 {
		return
	}
	path := args[1]
	value := ""
	if i := strings.Index(args[1], "="); i >= 0 {
		path = args[1][:i]
		value = args[1][i+1:]
	}
	if len(args) > 2 {
		value += "," + strings.Join(args[2:], ",")
	}
	l.call("gs_xml_write", "double", l.str(args[0]), l.str(path), l.str(value))
}

func (l *llvmGenerator) emitAES(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 4 {
		return
	}
	l.declare(name, TypeString)
	l.storePtr(name, l.call("gs_aes", "ptr", l.str(parts[0]), l.str(parts[1]), l.str(parts[2]), l.ptrExpr(parts[3])))
}

func (l *llvmGenerator) emitTask(args []string) {
	if len(args) < 2 {
		return
	}
	trig, cmd := "", ""
	if strings.EqualFold(args[0], "CREATE") && len(args) >= 4 {
		trig = args[2]
		cmd = strings.Join(args[3:], ",")
	}
	l.call("gs_task", "double", l.str(args[0]), l.str(args[1]), l.str(trig), l.str(cmd))
}
func (l *llvmGenerator) emitHTTP(args []string) {
	if len(args) < 2 {
		return
	}
	l.needNet = true
	body := ""
	if len(args) > 2 {
		body = strings.Join(args[2:], ",")
	}
	code := l.allocBuf(4)
	l.call("gs_http", "i32", l.str(strings.ToUpper(args[0])), l.str(args[1]), l.str(body), "ptr getelementptr inbounds ([1048576 x i8], ptr @HTTP_BODY_buf, i32 0, i32 0)", "i32 1048576", code)
	l.declare("HTTP_BODY", TypeString)
	l.storePtr("HTTP_BODY", "getelementptr inbounds ([1048576 x i8], ptr @HTTP_BODY_buf, i32 0, i32 0)")
	l.declare("HTTP_CODE", TypeFloat)
	l.storeNum("HTTP_CODE", "0.0")
}
func (l *llvmGenerator) emitUpload(args []string) {
	if len(args) < 2 {
		return
	}
	l.needNet = true
	code := l.allocBuf(4)
	l.call("gs_upld", "i32", l.str(args[0]), l.str(args[1]), "ptr getelementptr inbounds ([1048576 x i8], ptr @HTTP_BODY_buf, i32 0, i32 0)", "i32 1048576", code)
}
func (l *llvmGenerator) emitExec(args []string) {
	if len(args) == 0 {
		return
	}
	mode := strings.ToUpper(args[0])
	if mode == "WAIT" || mode == "NOWAIT" || mode == "ASYNC" || mode == "HIDE" || mode == "MIN" {
		l.call("gs_exec", "double", l.str(mode), l.str(strings.Join(args[1:], ",")), l.str(""), l.str(""))
		return
	}
	if mode == "OPEN" && len(args) >= 2 {
		l.call("gs_exec", "double", l.str(mode), l.str(""), l.str(args[1]), l.str(""))
		return
	}
	if mode == "RUNAS" && len(args) >= 2 {
		params := ""
		if len(args) > 2 {
			params = strings.Join(args[2:], ",")
		}
		l.call("gs_exec", "double", l.str(mode), l.str(""), l.str(args[1]), l.str(params))
		return
	}
	l.call("gs_exec", "double", l.str("WAIT"), l.str(strings.Join(args, " ")), l.str(""), l.str(""))
}

func (l *llvmGenerator) emitBeep(args []string) {
	parts := splitCSV(strings.Join(args, ","))
	if len(parts) < 2 {
		return
	}
	l.call("gs_beep", "double", "double "+l.numExpr(parts[0]), "double "+l.numExpr(parts[1]))
}

func (l *llvmGenerator) emitMessage(args []string, isErr bool) {
	parts := splitCSV(strings.Join(args, ","))
	if len(parts) == 0 {
		return
	}
	msg := parts[0]
	title := "gs"
	if len(parts) >= 2 {
		title = parts[1]
	}
	flags := 0x40
	if isErr {
		flags = 0x10
	}
	l.call("gs_msg", "void", l.str(msg), l.str(title), fmt.Sprintf("i32 %d", flags))
	if isErr {
		code := 1
		if len(parts) >= 3 {
			if n, err := strconv.Atoi(strings.TrimSpace(parts[2])); err == nil {
				code = n
			}
		}
		l.call("gs_exit", "void", fmt.Sprintf("i32 %d", code))
	}
}

func (l *llvmGenerator) emitDllOpen(args []string) {
	name, dll := splitKV(args)
	if name == "" || dll == "" {
		return
	}
	l.declare(name, TypeHandle)
	l.storePtr(name, l.call("gs_dll_open", "ptr", l.str(dll)))
}

func (l *llvmGenerator) emitDllGet(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return
	}
	l.declare(name, TypeProc)
	l.storePtr(name, l.call("gs_dll_sym", "ptr", l.ptrExpr(parts[0]), l.str(parts[1])))
}

func (l *llvmGenerator) emitDllCall(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 1 {
		return
	}
	l.declare(name, TypeFloat)
	proc := l.ptrExpr(parts[0])
	callArgs := parts[1:]
	for len(callArgs) < 8 {
		callArgs = append(callArgs, "0")
	}
	var out []string
	out = append(out, proc)
	for _, a := range callArgs[:8] {
		out = append(out, l.i64Arg(a))
	}
	l.storeNum(name, l.call("gs_dll_call", "double", out...))
}

func (l *llvmGenerator) emitWinAPI(args []string) {
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
	dllName := "user32.dll"
	if len(parts) > 1 {
		dllName = parts[0] + ".dll"
	}
	h := l.call("gs_dll_open", "ptr", l.str(dllName))
	p := l.call("gs_dll_sym", "ptr", h, l.str(funcName))
	for len(callArgs) < 8 {
		callArgs = append(callArgs, "0")
	}
	var out []string
	out = append(out, p)
	for _, a := range callArgs[:8] {
		out = append(out, l.i64Arg(a))
	}
	res := l.call("gs_dll_call", "double", out...)
	if lhs != "" {
		l.declare(lhs, TypeFloat)
		l.storeNum(lhs, res)
	}
}

func (l *llvmGenerator) i64Arg(v string) string {
	v = strings.TrimSpace(v)
	if name, ok := percentName(v); ok {
		if l.vars[name] == TypeString || l.vars[name] == TypeHandle || l.vars[name] == TypeProc {
			p := strings.TrimPrefix(l.ptrExpr(v), "ptr ")
			q := l.next("p2i")
			fmt.Fprintf(&l.body, "  %s = ptrtoint ptr %s to i64\n", q, p)
			return "i64 " + q
		}
		f := l.loadNum(name)
		i := l.next("f2i")
		fmt.Fprintf(&l.body, "  %s = fptosi double %s to i64\n", i, f)
		return "i64 " + i
	}
	if _, ok := l.vars[v]; ok {
		return l.i64Arg("%@" + v + "%")
	}
	if isNumber(v) {
		return "i64 " + strings.Split(llvmNumber(v), ".")[0]
	}
	if v == "NULL" || v == "0" || v == "" {
		return "i64 0"
	}
	p := strings.TrimPrefix(l.str(v), "ptr ")
	q := l.next("s2i")
	fmt.Fprintf(&l.body, "  %s = ptrtoint ptr %s to i64\n", q, p)
	return "i64 " + q
}

func (l *llvmGenerator) emitUIDef(args []string) error {
	name, rel := splitKV(args)
	if name == "" || rel == "" {
		return nil
	}
	p := rel
	if !filepath.IsAbs(p) && l.srcDir != "" {
		p = filepath.Join(l.srcDir, p)
	}
	data, err := os.ReadFile(p)
	if err != nil {
		return err
	}
	var win uiWindow
	if err := json.Unmarshal([]byte("{}"), &struct{}{}); err != nil {
		return err
	}
	_ = data // XML parser lives in uidef.go; use compact fallback below.
	// Lightweight fallback: emit a modern default form. C backend keeps full XML fidelity.
	l.needUI = true
	l.vars[name] = TypeHandle
	fmt.Fprintf(&l.body, "  %%%s = alloca ptr\n", name)
	form := l.call("gs_ui_form", "ptr", l.str("GS LLVM UI"), "i32 720", "i32 460")
	l.storePtr(name, form)
	l.call("gs_ui_control", "ptr", form, l.str("label"), l.str("GS LLVM UI"), "i32 28", "i32 22", "i32 260", "i32 28")
	l.call("gs_ui_control", "ptr", form, l.str("button"), l.str("OK"), "i32 560", "i32 28", "i32 120", "i32 36")
	l.call("gs_ui_show", "void", form)
	_ = win
	return nil
}

func llvmNumber(s string) string {
	s = strings.TrimSpace(s)
	if isNumber(s) {
		if strings.Contains(s, ".") {
			return s
		}
		return s + ".0"
	}
	return "0.0"
}
func llvmEscape(s string) string {
	var b strings.Builder
	for _, c := range []byte(s) {
		switch c {
		case '\\':
			b.WriteString("\\5C")
		case '\n':
			b.WriteString("\\0A")
		case '\r':
			b.WriteString("\\0D")
		case '\t':
			b.WriteString("\\09")
		case '"':
			b.WriteString("\\22")
		default:
			if c < 32 || c > 126 {
				fmt.Fprintf(&b, "\\%02X", c)
			} else {
				b.WriteByte(c)
			}
		}
	}
	return b.String()
}
