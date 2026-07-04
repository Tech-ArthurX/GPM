package gs

import "testing"

func TestPhaseAliasesParse(t *testing.T) {
	prog, err := ParseString(`
_PRE
  LOGS INFO,pre
_END
_INSTALL
  LOGS INFO,install
_END
_POSTINSTALL
  LOGS INFO,post
_END
_PREUNINSTALL
  LOGS INFO,preun
_END
_UNINST
  LOGS INFO,uninst
_END
_POSTUNINSTALL
  LOGS INFO,postun
_END
`)
	if err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"PREINST", "INSTALLING", "POSTINST", "PREUNINST", "UNINSTALLING", "POSTUNINST"} {
		if _, ok := prog.Subs[name]; !ok {
			t.Fatalf("missing normalized phase %s", name)
		}
	}
}
