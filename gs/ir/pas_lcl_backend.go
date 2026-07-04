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

type pasProgram struct {
	body strings.Builder
	vars map[string]string
}

func (g *Generator) compilePasLCL(outputPath string) error {
	p := &pasProgram{vars: make(map[string]string)}
	if err := g.pasEmitProgram(p); err != nil {
		return err
	}
	lprPath := outputPath + ".lpr"
	if err := os.WriteFile(lprPath, []byte(p.render()), 0644); err != nil {
		return err
	}
	outPath, err := filepath.Abs(outputPath)
	if err != nil {
		return err
	}
	outDir := filepath.Dir(outPath)
	unitDir := outputPath + ".fpc"
	if err := os.MkdirAll(unitDir, 0755); err != nil {
		return err
	}
	fpc, err := findFPC()
	if err != nil {
		return err
	}
	args := []string{
		"-MObjFPC", "-Sh", "-O2",
		"-FE" + outDir,
		"-FU" + unitDir,
	}
	args = append(args, lazarusUnitArgs()...)
	args = append(args, lprPath)
	cmd := exec.Command(fpc, args...)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("fpc lcl build failed: %s", out)
	}
	return nil
}

func (p *pasProgram) render() string {
	var out strings.Builder
	out.WriteString("program gs_pas_lcl;\n")
	out.WriteString("{$mode objfpc}{$H+}\n")
	out.WriteString("uses\n")
	out.WriteString("  Interfaces, Forms, StdCtrls, SysUtils, Windows;\n\n")
	out.WriteString("var\n")
	out.WriteString("  Form1: TForm;\n")
	for name, typ := range p.vars {
		fmt.Fprintf(&out, "  %s: %s;\n", name, typ)
	}
	out.WriteString("\nbegin\n")
	out.WriteString("  Application.Initialize;\n")
	out.WriteString("  Form1 := TForm.Create(nil);\n")
	out.WriteString(p.body.String())
	out.WriteString("end.\n")
	return out.String()
}

func (g *Generator) pasEmitProgram(p *pasProgram) error {
	for _, stmt := range g.prog.Main {
		cmd := cleanCmd(stmt.Cmd)
		args := cleanArgs(stmt.Args)
		switch cmd {
		case "SETV":
			g.pasSet(p, args, "")
		case "STRV":
			g.pasSet(p, args, "String")
		case "FLOT":
			g.pasSet(p, args, "Double")
		case "BOOL":
			g.pasSet(p, args, "Boolean")
		case "CALC":
			g.pasCalc(p, args)
		case "LOGS":
			g.pasLogs(p, args)
		case "BEEP":
			g.pasBeep(p, args)
		case "MBOX":
			g.pasMsg(p, args, false)
		case "EROR":
			g.pasMsg(p, args, true)
		case "UIDF":
			if err := g.pasUIDef(p, args); err != nil {
				return err
			}
		case "UILP":
			p.body.WriteString("  Form1.Show;\n")
			p.body.WriteString("  Application.Run;\n")
		case "EXEC":
			// intentionally not implemented for Pascal backend yet
		default:
			if cmd != "" {
				return fmt.Errorf("line %d: paslcl backend does not support command %q", stmt.Line, cmd)
			}
		}
	}
	return nil
}

func (g *Generator) pasDeclare(p *pasProgram, name, typ string) {
	if _, ok := p.vars[name]; ok {
		return
	}
	p.vars[name] = typ
}

func (g *Generator) pasSet(p *pasProgram, args []string, forced string) {
	name, val := splitKV(args)
	if name == "" {
		return
	}
	typ := forced
	if typ == "" {
		typ = inferPasType(val)
	}
	g.pasDeclare(p, name, typ)
	fmt.Fprintf(&p.body, "  %s := %s;\n", name, pasValue(val, typ))
}

func (g *Generator) pasCalc(p *pasProgram, args []string) {
	name, expr := splitKV(args)
	if name == "" {
		return
	}
	g.pasDeclare(p, name, "Double")
	fmt.Fprintf(&p.body, "  %s := %s;\n", name, pasExpr(expr))
}

func (g *Generator) pasLogs(p *pasProgram, args []string) {
	msg := joinPayload(args)
	vars := extractPercentVars(msg)
	if len(vars) == 0 {
		fmt.Fprintf(&p.body, "  WriteLn(%s);\n", pasString(msg))
		return
	}
	parts := []string{pasString(replacePercentWithFmt(msg))}
	for _, v := range vars {
		parts = append(parts, v)
	}
	fmt.Fprintf(&p.body, "  WriteLn(Format(%s, [%s]));\n", parts[0], strings.Join(parts[1:], ", "))
}

func (g *Generator) pasBeep(p *pasProgram, args []string) {
	parts := splitCSV(strings.Join(args, ","))
	if len(parts) < 2 {
		return
	}
	fmt.Fprintf(&p.body, "  Windows.Beep(%s, %s);\n", pasExpr(parts[0]), pasExpr(parts[1]))
}

func (g *Generator) pasMsg(p *pasProgram, args []string, isErr bool) {
	parts := splitCSV(strings.Join(args, ","))
	if len(parts) == 0 {
		return
	}
	msg := parts[0]
	title := "gs"
	if len(parts) >= 2 {
		title = parts[1]
	}
	flags := "MB_OK or MB_ICONINFORMATION"
	if isErr {
		flags = "MB_OK or MB_ICONERROR"
	}
	fmt.Fprintf(&p.body, "  MessageBoxA(0, PChar(%s), PChar(%s), %s);\n", pasString(msg), pasString(title), flags)
	if isErr {
		code := "1"
		if len(parts) >= 3 {
			code = strings.TrimSpace(parts[2])
		}
		fmt.Fprintf(&p.body, "  Halt(%s);\n", code)
	}
}

func (g *Generator) pasUIDef(p *pasProgram, args []string) error {
	_, rel := splitKV(args)
	if rel == "" {
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
		win.Title = "GS LCL"
	}
	if win.Width == 0 {
		win.Width = 640
	}
	if win.Height == 0 {
		win.Height = 480
	}
	fmt.Fprintf(&p.body, "  Form1.Caption := %s;\n", pasString(win.Title))
	fmt.Fprintf(&p.body, "  Form1.SetBounds(100, 100, %d, %d);\n", win.Width, win.Height)
	for i, c := range win.Controls {
		g.pasControl(p, i, &c)
	}
	return nil
}

func (g *Generator) pasControl(p *pasProgram, idx int, c *uiControl) {
	varName := fmt.Sprintf("Ctl%d", idx)
	typ, ctor := pasControlType(c.XMLName.Local)
	g.pasDeclare(p, varName, typ)
	fmt.Fprintf(&p.body, "  %s := %s.Create(Form1);\n", varName, ctor)
	fmt.Fprintf(&p.body, "  %s.Parent := Form1;\n", varName)
	if c.Text != "" {
		fmt.Fprintf(&p.body, "  %s.Caption := %s;\n", varName, pasString(c.Text))
	}
	if c.W == 0 {
		c.W = 80
	}
	if c.H == 0 {
		c.H = 24
	}
	fmt.Fprintf(&p.body, "  %s.SetBounds(%d, %d, %d, %d);\n", varName, c.X, c.Y, c.W, c.H)
}

func pasControlType(kind string) (string, string) {
	switch strings.ToLower(kind) {
	case "button":
		return "TButton", "TButton"
	case "label":
		return "TLabel", "TLabel"
	case "edit":
		return "TEdit", "TEdit"
	case "check":
		return "TCheckBox", "TCheckBox"
	case "group":
		return "TGroupBox", "TGroupBox"
	case "list":
		return "TListBox", "TListBox"
	default:
		return "TLabel", "TLabel"
	}
}

func inferPasType(v string) string {
	v = strings.TrimSpace(v)
	if strings.EqualFold(v, "true") || strings.EqualFold(v, "false") {
		return "Boolean"
	}
	if _, err := strconv.ParseFloat(v, 64); err == nil {
		return "Double"
	}
	return "String"
}

func pasValue(v, typ string) string {
	v = strings.TrimSpace(v)
	switch typ {
	case "String":
		return pasString(v)
	case "Boolean":
		if strings.EqualFold(v, "true") || v == "1" {
			return "True"
		}
		return "False"
	default:
		return pasExpr(v)
	}
}

func pasExpr(s string) string { return strings.TrimSpace(s) }
func pasString(s string) string {
	return "'" + strings.ReplaceAll(strings.Trim(s, "\"'"), "'", "''") + "'"
}

func extractPercentVars(s string) []string {
	var out []string
	for {
		start := strings.Index(s, "%")
		if start < 0 {
			break
		}
		rest := s[start+1:]
		end := strings.Index(rest, "%")
		if end < 0 {
			break
		}
		name := rest[:end]
		if name != "" {
			out = append(out, name)
		}
		s = rest[end+1:]
	}
	return out
}

func replacePercentWithFmt(s string) string {
	for {
		start := strings.Index(s, "%")
		if start < 0 {
			break
		}
		rest := s[start+1:]
		end := strings.Index(rest, "%")
		if end < 0 {
			break
		}
		s = s[:start] + "%s" + rest[end+1:]
	}
	return s
}
