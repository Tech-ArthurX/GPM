package gs

import (
	"strings"
	"testing"
)

func TestRuntimeErrorsAreConcise(t *testing.T) {
	err := RunSource("WAIT nope\n", t.TempDir(), HostCaps{}, nil)
	if err == nil {
		t.Fatal("expected error")
	}
	got := err.Error()
	if !strings.Contains(got, "line 1: WAIT: not an integer: nope") {
		t.Fatalf("unexpected error: %s", got)
	}
	if strings.Contains(got, "WAIT: WAIT:") {
		t.Fatalf("command prefix duplicated: %s", got)
	}
}

func TestFileErrorsDoNotLeakResolvedPaths(t *testing.T) {
	err := RunSource("JSON VALUE=missing.json,$.x\n", t.TempDir(), HostCaps{}, nil)
	if err == nil {
		t.Fatal("expected error")
	}
	got := err.Error()
	if !strings.Contains(got, "line 1: JSON: read missing.json: file not found") {
		t.Fatalf("unexpected error: %s", got)
	}
	if strings.Contains(got, t.TempDir()) {
		t.Fatalf("resolved temp path leaked: %s", got)
	}
}
