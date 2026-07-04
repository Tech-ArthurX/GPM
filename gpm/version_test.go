package main

import "testing"

func TestCompareVersions(t *testing.T) {
	cases := []struct {
		a, b string
		want int
	}{
		{"1.0", "1.0", 0},
		{"1.0", "1.1", -1},
		{"1.1", "1.0", 1},
		{"1.2.3", "1.2.3", 0},
		{"1.2.3", "1.2.4", -1},
		{"1.2.4", "1.2.3", 1},
		{"1.10", "1.9", 1},
		{"1.9", "1.10", -1},
		{"2.0", "1.99.99", 1},
		{"1.0.0", "1", 0},
		{"1", "1.0.0", 0},
		{"1.0", "1.0.0", 0},
		{"1.0.0.0", "1.0", 0},
		{"1.0.1", "1.0", 1},
		{"1.0", "1.0.1", -1},
		{"v1.0", "1.0", 0},
		{"v1.0", "v1.1", -1},
		{"1.0-beta", "1.0", 0},
		{"1.0-beta", "1.0.1", -1},
		{"", "1.0", -1},
		{"1.0", "", 1},
		{"", "", 0},
		{"1.0a", "1.0", 0},
		{"10.0.0", "9.99.99", 1},
	}
	for _, tc := range cases {
		got := CompareVersions(tc.a, tc.b)
		if got != tc.want {
			t.Errorf("CompareVersions(%q, %q) = %d, want %d", tc.a, tc.b, got, tc.want)
		}
	}
}

func TestParseVersion(t *testing.T) {
	cases := []struct {
		in   string
		want []int
	}{
		{"1.0", []int{1, 0}},
		{"v1.2.3", []int{1, 2, 3}},
		{"1.10.0", []int{1, 10, 0}},
		{"1.0-beta", []int{1, 0}},
		{"1.0.1-rc2", []int{1, 0, 1}},
		{"", []int{0}},
		{"abc", []int{0}},
		{"1.0a", []int{1, 0}},
	}
	for _, tc := range cases {
		got := parseVersion(tc.in)
		if !equalInts(got, tc.want) {
			t.Errorf("parseVersion(%q) = %v, want %v", tc.in, got, tc.want)
		}
	}
}

func equalInts(a, b []int) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}
