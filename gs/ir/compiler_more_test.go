package ir

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/SECTL/GPM/gs"
)

func TestGeneratorHighFrequencyCommands(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "data.json"), []byte(`{"a":{"b":"ok"},"items":[1,2,3]}`), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "test.xml"), []byte(`<root><item>val</item></root>`), 0644); err != nil {
		t.Fatal(err)
	}
	prog, err := gs.ParseString(`
SETV X=3
FUNC helper
  FILE WRITE,tmp/func.txt,ok
CALL helper
CALC Y=X+4
IFEX %Y% == 7,FILE,WRITE,out.txt,ok
WHEN %Y% >= 7
  FDIR MAKE,tmp
  FILE WRITE,tmp/a.txt,%Y%
  LINK HARD,tmp/a.txt,tmp/a-hard.txt
_END
LOOP 2
  FILE APPEND,tmp/loop.txt,%INDEX%
_END
if %Y% == 7
  FILE WRITE,tmp/if.txt,yes
else
  FILE WRITE,tmp/if.txt,no
EXEC NOWAIT,notepad.exe
EXEC HIDE,cmd.exe /c exit
EXEC OPEN,readme.txt
EXEC RUNAS,cmd.exe,/c whoami
FDIR LIST,tmp,FILES
JSON J=data.json,$.a.b
JSNL N=data.json,$.items
JSNS data.json,$.a.c=ok
REGI SET,HKCU\Software\GPMGSTest,Name,Value,SZ
REGI GET,HKCU\Software\GPMGSTest,Name,REGVAL
REGI DEL,HKCU\Software\GPMGSTest,Name
SERV STATUS,Spooler
TASK QUERY,\Microsoft\Windows\Defrag\ScheduledDefrag
FWAL ADD,name=GPMGSTest dir=in action=allow program=C:\Windows\System32\notepad.exe
FWAL DEL,name=GPMGSTest
HTTP GET,https://example.com
HTTP POST,https://example.com/api,{"x":1}
DOWN https://example.com/file.zip, tmp/file.zip
UPLD tmp/a.txt, https://example.com/upload
FORX *.txt,tmp
  FILE APPEND,tmp/list.txt,%FILE%
_END
; new compiler commands
STRL S=hello
LPOS P=hello,l
RPOS R=hello,l
FEXT EXT=file.txt
FDRV DRV=C:\path
EXIST E=tmp
LSTR LS=hello,2
RSTR RS=hello,3
MSTR MS=hello,1,2
RGEX MATCH=@hello123,[a-z]+
RGSB result,@hello world,world,earth
ENVI MYVAR=test
WAIT 10
EXIT 0
`)
	if err != nil {
		t.Fatal(err)
	}
	gen := NewGenerator(prog, `C:\LLVM\bin`)
	gen.SetSourceDir(dir)
	if err := gen.generate(); err != nil {
		t.Fatal(err)
	}
	code := gen.code.String()
	for _, want := range []string{"func.txt", "if (", "gs_dir_make", "gs_file_write", "gs_link", "for (int", "INDEX =", "gs_dir_list", "FindFirstFileA", "FILE =", "J = \"ok\"", "N = 3", "gs_json_set", "gs_reg_set", "gs_reg_get", "gs_reg_del", "gs_service", "gs_task", "gs_firewall", "gs_http", "gs_down", "gs_upld", "HTTP_BODY", "HTTP_CODE", "ShellExecuteA", "WinExec", "gs_lstr", "gs_rstr", "gs_mstr", "gs_regex", "gs_regex_sub", "gs_env_set", "gs_strlen2", "gs_lpos2", "gs_rpos2", "gs_fext", "gs_fdrv", "gs_file_exist"} {
		if !strings.Contains(code, want) {
			t.Fatalf("generated code missing %q:\n%s", want, code)
		}
	}
}

func TestGeneratorAllCommands(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "test.xml"), []byte(`<root><item>val</item></root>`), 0644); err != nil {
		t.Fatal(err)
	}
	prog, err := gs.ParseString(`
SETV X=1
ZIPX test.zip, outdir
ZIPC archive.zip, sourcedir
TARX test.tar.gz, outdir
RUNS .
PECM LOAD,script.ini
WNSH script.lua
VHDM C:\test.vhd
VHDU C:\test.vhd
VHDC C:\test.vhd,100,DYNAMIC
XMLR V=test.xml,root.item
XMLW test.xml,root.item=newval
AESC E=ENC,hex:00,hex:00,hello
GPMI mypackage
GPMU mypackage
GPMV V=mypackage
; string ops
LSTR L=hello,2
RSTR R=hello,2
MSTR M=hello,1,3
RGEX G=@abc123,([a-z]+),1
RGSB S,@x y z,y,-
`)
	if err != nil {
		t.Fatal(err)
	}
	gen := NewGenerator(prog, `C:\LLVM\bin`)
	gen.SetSourceDir(dir)
	if err := gen.generate(); err != nil {
		t.Fatal(err)
	}
	code := gen.code.String()
	for _, want := range []string{"gs_zip_extract", "gs_zip_create", "gs_tar_extract", "gs_run_scripts", "gs_pecmd", "gs_winxshell", "gs_vhd_mount", "gs_vhd_unmount", "gs_vhd_create", "gs_xml_read2", "gs_xml_write2", "gs_aes", "gs_gpm_install", "gs_gpm_uninstall", "gs_gpm_version", "gs_lstr", "gs_rstr", "gs_mstr", "gs_regex", "gs_regex_sub"} {
		if !strings.Contains(code, want) {
			t.Fatalf("generated code missing %q:\n%s", want, code)
		}
	}
}
