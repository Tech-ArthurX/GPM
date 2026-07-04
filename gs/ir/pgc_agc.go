package ir

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

// genPGCBlock compiles Pascal code (between PGCB / PGCE) into a .obj using
// the local FPC compiler, then adds the .obj to the linker.
func (g *Generator) genPGCBlock(args []string) error {
	if len(args) == 0 {
		return nil
	}
	pasSrc := strings.TrimSpace(args[0])
	if pasSrc == "" {
		return nil
	}
	objName := fmt.Sprintf("pgc_%d", len(g.extraObjs))
	unitDir := filepath.Join(g.resultDir, "pgc")
	if err := os.MkdirAll(unitDir, 0755); err != nil {
		return err
	}
	pasPath := filepath.Join(unitDir, objName+".pas")
	// Wrap user code in a unit so FPC can produce a .obj
	pasFull := "unit " + objName + ";\n{$mode objfpc}{$H+}\ninterface\nimplementation\n" + pasSrc + "\nend.\n"
	if err := os.WriteFile(pasPath, []byte(pasFull), 0644); err != nil {
		return err
	}
	fpc, err := findFPC()
	if err != nil {
		return err
	}
	fpcArgs := []string{"-MObjFPC", "-Sh", "-FE" + unitDir, "-o" + objName + ".o",
		"-FU" + unitDir,
	}
	fpcArgs = append(fpcArgs, lazarusUnitArgs()...)
	fpcArgs = append(fpcArgs,
		// no main program, compile as unit
		"-s", pasPath)
	cmd := exec.Command(fpc, fpcArgs...)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("fpc pascal block failed: %s\n%s", err, out)
	}
	objFile := filepath.Join(unitDir, objName+".o")
	if _, err := os.Stat(objFile); err == nil {
		g.extraObjs = append(g.extraObjs, objFile)
	}
	// Add pascal unit to header so C code can call exported procedures
	g.cHeaderCode = append(g.cHeaderCode,
		"extern void __stdcall pgc_"+objName+"(void);")
	return nil
}

// genAGCBlock assembles x86-64 code (between AGCB / AGCE) into a .obj
// using the local NASM compiler, then adds the .obj to the linker.
func (g *Generator) genAGCBlock(args []string) error {
	if len(args) == 0 {
		return nil
	}
	asmSrc := strings.TrimSpace(args[0])
	if asmSrc == "" {
		return nil
	}
	objName := fmt.Sprintf("agc_%d", len(g.extraObjs))
	asmDir := filepath.Join(g.resultDir, "agc")
	if err := os.MkdirAll(asmDir, 0755); err != nil {
		return err
	}
	asmPath := filepath.Join(asmDir, objName+".asm")
	if err := os.WriteFile(asmPath, []byte(asmSrc), 0644); err != nil {
		return err
	}
	nasm := `C:\msys64\usr\bin\nasm.exe`
	objFile := filepath.Join(asmDir, objName+".obj")
	cmd := exec.Command(nasm, "-f", "win64", "-o", objFile, asmPath)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("nasm asm block failed: %s\n%s", err, out)
	}
	if _, err := os.Stat(objFile); err == nil {
		g.extraObjs = append(g.extraObjs, objFile)
	}
	// Declare entry point so C can link it
	g.cHeaderCode = append(g.cHeaderCode,
		"extern void __stdcall agc_"+objName+"(void);")
	return nil
}
