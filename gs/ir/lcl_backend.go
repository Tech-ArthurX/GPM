package ir

import (
	"encoding/xml"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
)

// removed duplicate SetBackend method - now defined in ir.go

type lclProgram struct {
	body    strings.Builder
	imports map[string]bool
	dlls    map[string]string // dll path -> Go var
	procs   map[string]string // dll+func -> Go var
	vars    map[string]ValType
	uiVars  map[string]string // gs name -> Go var
	uiCtls  map[string]string // gs name -> control kind for events maybe
	tmpIdx  int
}

func newLCLProgram() *lclProgram {
	return &lclProgram{
		imports: map[string]bool{`"syscall"`: true, `"unsafe"`: true, `"strings"`: true},
		dlls:    map[string]string{},
		procs:   map[string]string{},
		vars:    map[string]ValType{},
		uiVars:  map[string]string{},
		uiCtls:  map[string]string{},
	}
}

func (p *lclProgram) addImport(path string) { p.imports[path] = true }

func (p *lclProgram) ensureDLL(name string) string {
	if v, ok := p.dlls[name]; ok {
		return v
	}
	v := fmt.Sprintf("dll%d", len(p.dlls))
	p.dlls[name] = v
	p.addImport(`"syscall"`)
	return v
}

func (p *lclProgram) ensureProc(dll, fn string) string {
	key := dll + "::" + fn
	if v, ok := p.procs[key]; ok {
		return v
	}
	v := fmt.Sprintf("proc%d", len(p.procs))
	p.procs[key] = v
	return v
}

func (g *Generator) compileLCL(outputPath string) error {
	p := newLCLProgram()
	if err := g.lclEmitProgram(p); err != nil {
		return err
	}

	projectDir := outputPath + ".lcl"
	if err := os.MkdirAll(projectDir, 0755); err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(projectDir, "main.go"), []byte(p.render()), 0644); err != nil {
		return err
	}
	lclPath := strings.TrimSpace(os.Getenv("GS_LCL_PATH"))
	if lclPath == "" {
		return fmt.Errorf("GS_LCL_PATH is required for LCL output")
	}
	lclPath = filepath.Clean(lclPath)
	mod := fmt.Sprintf("module gs_lcl_app\n\ngo 1.20\n\nrequire github.com/energye/lcl v0.0.0\n\nreplace github.com/energye/lcl => %s\n", filepath.ToSlash(lclPath))
	if err := os.WriteFile(filepath.Join(projectDir, "go.mod"), []byte(mod), 0644); err != nil {
		return err
	}
	outPath, err := filepath.Abs(outputPath)
	if err != nil {
		return err
	}
	cmd := exec.Command("go", "build", "-o", outPath, ".")
	cmd.Dir = projectDir
	cmd.Env = append(os.Environ(), "CGO_ENABLED=0")
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("lcl go build failed: %s", out)
	}
	return nil
}

func (p *lclProgram) render() string {
	var out strings.Builder
	out.WriteString("package main\n\n")
	out.WriteString("import (\n")
	keys := make([]string, 0, len(p.imports))
	for k := range p.imports {
		if strings.HasPrefix(k, "__") {
			continue
		}
		keys = append(keys, k)
	}
	sortStrings(keys)
	for _, k := range keys {
		out.WriteString("    ")
		out.WriteString(k)
		out.WriteByte('\n')
	}
	out.WriteString(")\n\n")
	if len(p.dlls) > 0 {
		out.WriteString("var (\n")
		for name, v := range p.dlls {
			fmt.Fprintf(&out, "    %s = syscall.NewLazyDLL(%q)\n", v, name)
		}
		for k, v := range p.procs {
			parts := strings.SplitN(k, "::", 2)
			dllName := parts[0]
			fnName := parts[1]
			dllVar := p.dlls[dllName]
			fmt.Fprintf(&out, "    %s = %s.NewProc(%q)\n", v, dllVar, fnName)
		}
		out.WriteString(")\n\n")
	}
	out.WriteString("func gsCall(p *syscall.LazyProc, args ...uintptr) uintptr {\n")
	out.WriteString("    r, _, _ := p.Call(args...)\n")
	out.WriteString("    return r\n")
	out.WriteString("}\n\n")
	out.WriteString("func gsCStr(s string) uintptr {\n")
	out.WriteString("    b, _ := syscall.BytePtrFromString(s)\n")
	out.WriteString("    return uintptr(unsafe.Pointer(b))\n")
	out.WriteString("}\n\n")
	out.WriteString("func toInt(v any) int64 {\n")
	out.WriteString("    switch x := v.(type) {\n")
	out.WriteString("    case float64:\n        return int64(x)\n")
	out.WriteString("    case int:\n        return int64(x)\n")
	out.WriteString("    case bool:\n        if x { return 1 }\n        return 0\n")
	out.WriteString("    default:\n        return 0\n")
	out.WriteString("    }\n")
	out.WriteString("}\n\n")
	out.WriteString("func gsDLLCall(ref string, args ...uintptr) uintptr {\n")
	out.WriteString("    parts := strings.SplitN(ref, \"::\", 2)\n")
	out.WriteString("    if len(parts) != 2 { return 0 }\n")
	out.WriteString("    p := syscall.NewLazyDLL(parts[0]).NewProc(parts[1])\n")
	out.WriteString("    return gsCall(p, args...)\n")
	out.WriteString("}\n\n")
	out.WriteString("func main() {\n")
	out.WriteString(p.body.String())
	out.WriteString("}\n")
	return out.String()
}

func sortStrings(s []string) {
	for i := 1; i < len(s); i++ {
		for j := i; j > 0 && s[j-1] > s[j]; j-- {
			s[j-1], s[j] = s[j], s[j-1]
		}
	}
}

func (g *Generator) lclEmitProgram(p *lclProgram) error {
	formCreated := false
	ensureForm := func() {
		if formCreated {
			return
		}
		formCreated = true
		p.addImport(`. "github.com/energye/lcl/lcl"`)
		p.body.WriteString("    Init()\n")
		p.body.WriteString("    Application.Initialize()\n")
		p.body.WriteString("    form := NewForm(nil)\n")
		p.body.WriteString("    form.SetCaption(\"gs\")\n")
		p.body.WriteString("    form.SetBounds(100, 100, 640, 480)\n")
	}

	for _, stmt := range g.prog.Main {
		cmd := cleanCmd(stmt.Cmd)
		args := cleanArgs(stmt.Args)
		switch cmd {
		case "":
			// blank line, ignore
		case "SETV":
			g.lclGenSet(p, args, "")
		case "STRV":
			g.lclGenSet(p, args, "string")
		case "FLOT":
			g.lclGenSet(p, args, "float64")
		case "BOOL":
			g.lclGenSet(p, args, "bool")
		case "CALC":
			g.lclGenCalc(p, args)
		case "LOGS":
			g.lclGenLogs(p, args)
		case "EXEC":
			g.lclGenExec(p, args)
		case "BEEP":
			g.lclGenBeep(p, args)
		case "MBOX":
			g.lclGenMessageBox(p, args)
		case "EROR":
			g.lclGenError(p, args)
		case "WAPI", "APIC":
			g.lclGenWinAPI(p, args)
		case "DLLO", "DLOP":
			g.lclGenDllOpen(p, args)
		case "DLLG", "DLSY":
			g.lclGenDllGet(p, args)
		case "DLLC", "DLCA":
			g.lclGenDllCall(p, args)
		case "DLLF", "DLCL":
			// dynamic dlls are kept alive for process lifetime in LCL backend
		case "UIDF":
			ensureForm()
			if err := g.lclGenUIDef(p, args); err != nil {
				return err
			}
		case "UILP":
			ensureForm()
			p.body.WriteString("    form.Show()\n")
			p.body.WriteString("    Application.Run()\n")
		default:
			return fmt.Errorf("line %d: lcl backend does not support command %q", stmt.Line, cmd)
		}
	}
	if formCreated {
		// If user did not call UILP we still keep main returning normally.
	}
	return nil
}

func (g *Generator) lclGenSet(p *lclProgram, args []string, force string) {
	name, value := splitKV(args)
	if name == "" {
		return
	}
	typ := force
	if typ == "" {
		typ = inferGoType(value)
	}
	p.declare(name, typ)
	fmt.Fprintf(&p.body, "    %s = %s\n", name, p.goValue(value, typ))
}

func (g *Generator) lclGenCalc(p *lclProgram, args []string) {
	name, expr := splitKV(args)
	if name == "" {
		return
	}
	p.declare(name, "float64")
	fmt.Fprintf(&p.body, "    %s = %s\n", name, p.goExpr(expr))
}

func (g *Generator) lclGenLogs(p *lclProgram, args []string) {
	p.addImport(`"fmt"`)
	msg := joinPayload(args)
	vars := p.extractVars(msg)
	if len(vars) == 0 {
		fmt.Fprintf(&p.body, "    fmt.Println(%q)\n", msg)
		return
	}
	format := msg
	for _, v := range vars {
		format = strings.Replace(format, "%"+v+"%", "%v", 1)
	}
	fmt.Fprintf(&p.body, "    fmt.Printf(%q+\"\\n\"", format)
	for _, v := range vars {
		fmt.Fprintf(&p.body, ", %s", v)
	}
	p.body.WriteString(")\n")
}

func (g *Generator) lclGenExec(p *lclProgram, args []string) {
	p.addImport(`"os/exec"`)
	cmd := strings.Join(args, " ")
	if cmd == "" {
		return
	}
	fmt.Fprintf(&p.body, "    _ = exec.Command(%q).Run()\n", cmd)
}

func (g *Generator) lclGenBeep(p *lclProgram, args []string) {
	parts := splitCSV(strings.Join(args, ","))
	if len(parts) < 2 {
		return
	}
	dll := p.ensureDLL("kernel32.dll")
	_ = dll
	proc := p.ensureProc("kernel32.dll", "Beep")
	fmt.Fprintf(&p.body, "    gsCall(%s, uintptr(int64(%s)), uintptr(int64(%s)))\n", proc, p.goNumber(parts[0]), p.goNumber(parts[1]))
}

func (g *Generator) lclGenMessageBox(p *lclProgram, args []string) {
	parts := splitCSV(strings.Join(args, ","))
	if len(parts) == 0 {
		return
	}
	text := parts[0]
	title := "gs"
	if len(parts) >= 2 {
		title = parts[1]
	}
	kind := "info"
	if len(parts) >= 3 {
		kind = strings.ToLower(strings.TrimSpace(parts[2]))
	}
	flag := "0x40" // MB_ICONINFORMATION
	if kind == "error" || kind == "err" {
		flag = "0x10" // MB_ICONERROR
	}
	p.ensureDLL("user32.dll")
	proc := p.ensureProc("user32.dll", "MessageBoxA")
	fmt.Fprintf(&p.body, "    gsCall(%s, 0, gsCStr(%s), gsCStr(%s), %s)\n", proc, p.goString(text), p.goString(title), flag)
	p.addImport(`"unsafe"`)
}

func (g *Generator) lclGenError(p *lclProgram, args []string) {
	parts := splitCSV(strings.Join(args, ","))
	if len(parts) == 0 {
		return
	}
	msg := parts[0]
	title := "gs error"
	if len(parts) >= 2 {
		title = parts[1]
	}
	code := 1
	if len(parts) >= 3 {
		if n, err := strconv.Atoi(strings.TrimSpace(parts[2])); err == nil {
			code = n
		}
	}
	p.ensureDLL("user32.dll")
	proc := p.ensureProc("user32.dll", "MessageBoxA")
	p.addImport(`"unsafe"`)
	p.addImport(`"os"`)
	fmt.Fprintf(&p.body, "    gsCall(%s, 0, gsCStr(%s), gsCStr(%s), 0x10)\n", proc, p.goString(msg), p.goString(title))
	fmt.Fprintf(&p.body, "    os.Exit(%d)\n", code)
}

func (g *Generator) lclGenWinAPI(p *lclProgram, args []string) {
	lhs, rest := splitMaybeAssign(args)
	if rest == "" {
		return
	}
	parts := splitCSV(rest)
	if len(parts) == 0 {
		return
	}
	spec := parts[0]
	callArgs := parts[1:]
	dll := "user32.dll"
	fn := spec
	if dot := strings.Index(spec, "."); dot >= 0 {
		dll = spec[:dot] + ".dll"
		fn = spec[dot+1:]
	}
	p.ensureDLL(dll)
	proc := p.ensureProc(dll, fn)
	p.addImport(`"unsafe"`)
	goArgs := make([]string, len(callArgs))
	for i, a := range callArgs {
		goArgs[i] = p.goCallArg(a)
	}
	if lhs != "" {
		p.declare(lhs, "float64")
		fmt.Fprintf(&p.body, "    %s = float64(gsCall(%s%s))\n", lhs, proc, joinCallArgs(goArgs))
	} else {
		fmt.Fprintf(&p.body, "    gsCall(%s%s)\n", proc, joinCallArgs(goArgs))
	}
}

func (g *Generator) lclGenDllOpen(p *lclProgram, args []string) {
	name, dll := splitKV(args)
	if name == "" || dll == "" {
		return
	}
	p.declare(name, "string")
	fmt.Fprintf(&p.body, "    %s = %q\n", name, dll)
	p.ensureDLL(dll)
}

func (g *Generator) lclGenDllGet(p *lclProgram, args []string) {
	name, rest := splitKV(args)
	if name == "" || rest == "" {
		return
	}
	parts := splitCSV(rest)
	if len(parts) < 2 {
		return
	}
	dllVar := strings.TrimSpace(parts[0])
	fn := strings.TrimSpace(parts[1])
	p.declare(name, "string")
	fmt.Fprintf(&p.body, "    %s = %s + \"::\" + %q\n", name, dllVar, fn)
	// Ensure proc reference is staged lazily when called.
}

func (g *Generator) lclGenDllCall(p *lclProgram, args []string) {
	name, rest := splitKV(args)
	if name == "" || rest == "" {
		return
	}
	parts := splitCSV(rest)
	if len(parts) < 1 {
		return
	}
	procRef := strings.TrimSpace(parts[0])
	callArgs := parts[1:]
	for len(callArgs) < 8 {
		callArgs = append(callArgs, "0")
	}
	goArgs := make([]string, len(callArgs))
	for i, a := range callArgs {
		goArgs[i] = p.goCallArg(a)
	}
	p.addImport(`"unsafe"`)
	p.addImport(`"syscall"`)
	p.declare(name, "float64")
	fmt.Fprintf(&p.body, "    %s = float64(gsDLLCall(%s%s))\n", name, procRef, joinCallArgs(goArgs))
	p.addImport(`"strings"`)
	// helper function declared once
	if _, ok := p.imports["__gsDLLCall"]; !ok {
		p.imports["__gsDLLCall"] = true
	}
}

func (g *Generator) lclGenUIDef(p *lclProgram, args []string) error {
	name, rel := splitKV(args)
	if name == "" || rel == "" {
		return fmt.Errorf("UIDF needs NAME = file.ui")
	}
	path := rel
	if !filepath.IsAbs(path) && g.srcDir != "" {
		path = filepath.Join(g.srcDir, rel)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	var win uiWindow
	if err := xml.Unmarshal(data, &win); err != nil {
		return err
	}
	if win.Title == "" {
		win.Title = name
	}
	if win.Width == 0 {
		win.Width = 640
	}
	if win.Height == 0 {
		win.Height = 480
	}
	fmt.Fprintf(&p.body, "    form.SetCaption(%q)\n", win.Title)
	fmt.Fprintf(&p.body, "    form.SetBounds(100, 100, %d, %d)\n", int32(win.Width), int32(win.Height))
	p.uiVars[name] = "form"
	for i, c := range win.Controls {
		goName := fmt.Sprintf("ctrl_%s_%d", strings.ToLower(c.XMLName.Local), i)
		ctor := lclConstructor(c.XMLName.Local)
		fmt.Fprintf(&p.body, "    %s := %s(form)\n", goName, ctor)
		fmt.Fprintf(&p.body, "    %s.SetParent(form)\n", goName)
		if c.Text != "" {
			fmt.Fprintf(&p.body, "    %s.SetCaption(%q)\n", goName, c.Text)
		}
		if c.W == 0 {
			c.W = 80
		}
		if c.H == 0 {
			c.H = 24
		}
		fmt.Fprintf(&p.body, "    %s.SetBounds(%d, %d, %d, %d)\n", goName, int32(c.X), int32(c.Y), int32(c.W), int32(c.H))
		if c.ID != "" {
			p.uiVars[c.ID] = goName
			p.uiCtls[c.ID] = strings.ToLower(c.XMLName.Local)
		}
	}
	return nil
}

// ---- helpers ---------------------------------------------------------------

func (p *lclProgram) declare(name, typ string) {
	if _, ok := p.vars[name]; ok {
		return
	}
	p.vars[name] = ValType(0) // marker only; LCL backend tracks go types via string map below
	if typ == "" {
		typ = "any"
	}
	fmt.Fprintf(&p.body, "    var %s %s = %s\n", name, typ, goZero(typ))
}

func (p *lclProgram) extractVars(s string) []string {
	var out []string
	for v := range p.vars {
		if strings.Contains(s, "%"+v+"%") {
			out = append(out, v)
		}
	}
	return out
}

func (p *lclProgram) goExpr(expr string) string {
	expr = strings.TrimSpace(expr)
	for v := range p.vars {
		expr = strings.ReplaceAll(expr, v, "("+v+")")
	}
	return expr
}

func (p *lclProgram) goValue(v, typ string) string {
	v = strings.TrimSpace(v)
	if v == "" {
		return goZero(typ)
	}
	switch typ {
	case "string":
		return strconv.Quote(stripQuotes(v))
	case "bool":
		if strings.EqualFold(v, "true") || v == "1" {
			return "true"
		}
		return "false"
	default:
		if _, ok := p.vars[v]; ok {
			return v
		}
		if _, err := strconv.ParseFloat(v, 64); err == nil {
			return v
		}
		return strconv.Quote(v)
	}
}

func (p *lclProgram) goString(v string) string {
	v = strings.TrimSpace(v)
	if _, ok := p.vars[v]; ok {
		return v
	}
	return strconv.Quote(stripQuotes(v))
}

func (p *lclProgram) goNumber(v string) string {
	v = strings.TrimSpace(v)
	if _, ok := p.vars[v]; ok {
		return v
	}
	if _, err := strconv.ParseFloat(v, 64); err == nil {
		return v
	}
	return "0"
}

func (p *lclProgram) goCallArg(v string) string {
	v = strings.TrimSpace(v)
	if v == "" || v == "0" {
		return "0"
	}
	if _, ok := p.vars[v]; ok {
		return "uintptr(toInt(" + v + "))"
	}
	if _, err := strconv.ParseFloat(v, 64); err == nil {
		return "uintptr(int64(" + v + "))"
	}
	return "gsCStr(" + strconv.Quote(stripQuotes(v)) + ")"
}

func joinCallArgs(args []string) string {
	if len(args) == 0 {
		return ""
	}
	return ", " + strings.Join(args, ", ")
}

func inferGoType(v string) string {
	v = strings.TrimSpace(v)
	if strings.EqualFold(v, "true") || strings.EqualFold(v, "false") {
		return "bool"
	}
	if _, err := strconv.ParseFloat(v, 64); err == nil {
		return "float64"
	}
	return "string"
}

func goZero(typ string) string {
	switch typ {
	case "string":
		return "\"\""
	case "bool":
		return "false"
	default:
		return "0"
	}
}

func stripQuotes(s string) string {
	if len(s) >= 2 && s[0] == '"' && s[len(s)-1] == '"' {
		return s[1 : len(s)-1]
	}
	return s
}

func lclConstructor(kind string) string {
	switch strings.ToLower(kind) {
	case "button":
		return "NewButton"
	case "label":
		return "NewLabel"
	case "edit":
		return "NewEdit"
	case "list":
		return "NewListBox"
	case "check":
		return "NewCheckBox"
	case "group":
		return "NewGroupBox"
	default:
		return "NewLabel"
	}
}
