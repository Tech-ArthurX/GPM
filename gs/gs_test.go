package gs

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

type testLogger struct{ lines []string }

func (l *testLogger) Log(level, msg string) {
	l.lines = append(l.lines, level+":"+msg)
}

func TestRunFilePhasesAndRunScripts(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "legacy.bat"), []byte("@echo off\r\necho legacy>>legacy.out\r\n"), 0644); err != nil {
		t.Fatal(err)
	}
	script := `
_PREI
  FILE WRITE, pre.out, pre
_END
_INST
  RUNS .
_END
_POST
  SETV OK=1
  WHEN %@OK% == 1
    FILE WRITE, post.out, post
  _END
_END
`
	path := filepath.Join(dir, "gs")
	if err := os.WriteFile(path, []byte(script), 0644); err != nil {
		t.Fatal(err)
	}
	caps := HostCaps{AllowExec: true}
	if err := RunFilePhases(path, []string{"PREINST", "INSTALLING", "POSTINST"}, caps, &testLogger{}); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"pre.out", "post.out", "legacy.out"} {
		if _, err := os.Stat(filepath.Join(dir, name)); err != nil {
			t.Fatalf("expected %s: %v", name, err)
		}
	}
}

func TestSubSyntaxRequiresFunc(t *testing.T) {
	_, err := ParseString(`
_SUB old
  LOGS INFO, old
_END
`)
	if err == nil || !strings.Contains(err.Error(), "use FUNC") {
		t.Fatalf("expected FUNC guidance, got %v", err)
	}
}

func TestCoreCommands(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "data.json"), []byte(`{"a":{"b":1},"items":[1,2]}`), 0644); err != nil {
		t.Fatal(err)
	}
	script := `
JSON VALUE=data.json,$.a.b
JSNL COUNT=data.json,$.items
JSNS data.json,$.a.c=ok
HASH SHA=SHA256,@abc
BASE B64=ENC,hello
HEXC HEX=ENC,hi
AESC AES=ENC,hex:00112233445566778899aabbccddeeff,hex:0102030405060708090a0b0c0d0e0f10,hello
AESC DEC=DEC,hex:00112233445566778899aabbccddeeff,hex:0102030405060708090a0b0c0d0e0f10,%@AES%
FILE WRITE, vars.txt, %@VALUE%|%@COUNT%|%@B64%|%@HEX%|%@DEC%
`
	if err := RunSource(script, dir, HostCaps{}, &testLogger{}); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(filepath.Join(dir, "vars.txt"))
	if err != nil {
		t.Fatal(err)
	}
	got := string(data)
	for _, want := range []string{"1", "2", "aGVsbG8=", "6869", "hello"} {
		if !strings.Contains(got, want) {
			t.Fatalf("vars.txt missing %q: %q", want, got)
		}
	}
}

func TestControlFlowAndCalc(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "a.txt"), []byte("a"), 0644); err != nil {
		t.Fatal(err)
	}
	script := `
CALC X=1+2*3
FUNC writeFunc
  FILE WRITE, func.txt, ok
CALL writeFunc
IFEX %@X% == 7,FILE,WRITE,ifex.txt,ok
WHEN %@X% >= 7 AND %@X% < 8
  FILE WRITE, when.txt, ok
_END
LOOP 3
  FILE APPEND, loop.txt, %@INDEX%
_END
FORX *.txt, .
  FILE APPEND, files.txt, %@FILE%|
_END
if %@X% == 6
  FILE WRITE, simple.txt, bad
elif %@X% == 7
  FILE WRITE, simple.txt, good
else
  FILE WRITE, simple.txt, other
`
	if err := RunSource(script, dir, HostCaps{}, &testLogger{}); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"func.txt", "ifex.txt", "when.txt", "loop.txt", "files.txt", "simple.txt"} {
		if _, err := os.Stat(filepath.Join(dir, name)); err != nil {
			t.Fatalf("expected %s: %v", name, err)
		}
	}
	loop, _ := os.ReadFile(filepath.Join(dir, "loop.txt"))
	if string(loop) != "012" {
		t.Fatalf("loop output = %q", string(loop))
	}
	simple, _ := os.ReadFile(filepath.Join(dir, "simple.txt"))
	if string(simple) != "good" {
		t.Fatalf("simple output = %q", string(simple))
	}
}
