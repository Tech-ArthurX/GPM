package main

import "testing"

func TestSelectPackageFromIndexUsesLatestWhenVersionOmitted(t *testing.T) {
	index := []PackageIndexItem{
		{Name: "Demo", Version: "1.0"},
		{Name: "Demo", Version: "1.10"},
		{Name: "Demo", Version: "1.2"},
	}

	got := selectPackageFromIndex(index, "demo", "")
	if got == nil {
		t.Fatal("selectPackageFromIndex returned nil")
	}
	if got.Version != "1.10" {
		t.Fatalf("selected version = %q, want latest 1.10", got.Version)
	}
}

func TestSelectPackageFromIndexUsesExactVersion(t *testing.T) {
	index := []PackageIndexItem{
		{Name: "Demo", Version: "1.0"},
		{Name: "Demo", Version: "2.0"},
	}

	got := selectPackageFromIndex(index, "Demo", "1.0")
	if got == nil {
		t.Fatal("selectPackageFromIndex returned nil")
	}
	if got.Version != "1.0" {
		t.Fatalf("selected version = %q, want exact 1.0", got.Version)
	}
}

func TestInstallDialogIDRoundTripKeepsUnderscores(t *testing.T) {
	want := installDialogPayload{
		Action:           string(installConflictDowngrade),
		Name:             "pkg_with_under_score",
		Version:          "1.0_rc_2",
		InstalledVersion: "2.0_final",
	}

	id := makeInstallDialogID(want)
	got, ok := parseInstallDialogID(id)
	if !ok {
		t.Fatalf("parseInstallDialogID(%q) failed", id)
	}
	if got != want {
		t.Fatalf("payload = %#v, want %#v", got, want)
	}
}

func TestParseInstallDialogIDRejectsLegacyAmbiguousID(t *testing.T) {
	if _, ok := parseInstallDialogID("downgrade_pkg_with_under_score_1.0"); ok {
		t.Fatal("legacy underscore-delimited dialog id should be rejected")
	}
}
