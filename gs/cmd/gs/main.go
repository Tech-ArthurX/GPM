package main

import (
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/SECTL/GPM/gs"
	"github.com/SECTL/GPM/gs/ir"
)

func main() {
	llvmPath := flag.String("llvm", "", "LLVM bin path")
	backend := flag.String("backend", "llvm", "backend: llvm, c, or lcl")
	optimize := flag.String("opt", "2", "optimization level: 0=none, 1=basic, 2=full, 3=aggressive")
	flag.Parse()
	args := flag.Args()
	if len(args) < 1 {
		fmt.Println("Usage: gs <input.gs> -o <output.exe>")
		os.Exit(1)
	}

	var inputPath, outputPath string
	for i := 0; i < len(args); i++ {
		if args[i] == "-o" && i+1 < len(args) {
			outputPath = args[i+1]
			i++
		} else if inputPath == "" {
			inputPath = args[i]
		}
	}
	if inputPath == "" {
		fmt.Println("Error: need input file")
		os.Exit(1)
	}
	if outputPath == "" {
		outputPath = "a.exe"
	}
	if *backend != "lcl" && *llvmPath == "" {
		*llvmPath = findLLVM()
		if *llvmPath == "" {
			fmt.Println(`Error: LLVM/clang not found. Pass -llvm <bin-dir> (for example C:\Program Files\LLVM\bin) or install LLVM.`)
			os.Exit(1)
		}
	}

	data, err := os.ReadFile(inputPath)
	if err != nil {
		fmt.Printf("Error: read %s: %v\n", inputPath, err)
		os.Exit(1)
	}
	prog, err := gs.ParseString(string(data))
	if err != nil {
		fmt.Printf("Error: parse %s: %v\n", inputPath, err)
		os.Exit(1)
	}
	gen := ir.NewGenerator(prog, *llvmPath)
	gen.SetBackend(*backend)
	gen.SetOptLevel(*optimize)
	gen.SetSourceDir(filepath.Dir(inputPath))
	if err := gen.Compile(outputPath); err != nil {
		fmt.Printf("Error: compile: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("Compiled: %s\n", outputPath)
}

func findLLVM() string {
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
