package gs

import (
	"reflect"
	"strings"
	"testing"
)

func testResolveDrvPath(path string) string {
	if strings.HasPrefix(path, "-") || strings.HasPrefix(path, "/") {
		return path
	}
	return "ABS:" + path
}

func TestBuildDrvInstallArgs(t *testing.T) {
	tests := []struct {
		name string
		in   []string
		want []string
	}{
		{
			name: "source install with password config and progress",
			in:   []string{"B", `D:\Drivers`, "secret", "wifi.ini"},
			want: []string{"-b", `ABS:D:\Drivers`, "-p:secret", "-config:ABS:wifi.ini", "-Progress"},
		},
		{
			name: "offline auto install",
			in:   []string{"Y"},
			want: []string{"-y", "-Progress"},
		},
		{
			name: "migrate with filters",
			in:   []string{"MIGRATE", `E:\Windows`, "net;display"},
			want: []string{"-migrate", `ABS:E:\Windows`, "/filter:net;display"},
		},
		{
			name: "backup with target and filter",
			in:   []string{"BACKUP", `D:\driver`, `E:\Windows`, "net"},
			want: []string{"-backup", `ABS:D:\driver`, `ABS:E:\Windows`, "/filter:net"},
		},
		{
			name: "raw keeps switches and resolves config",
			in:   []string{"RAW", "-b", `D:\Drivers`, "-p:secret", "-config:wifi.ini"},
			want: []string{"-b", `ABS:D:\Drivers`, "-p:secret", "-config:ABS:wifi.ini"},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := buildDrvInstallArgs(tt.in, testResolveDrvPath)
			if err != nil {
				t.Fatal(err)
			}
			if !reflect.DeepEqual(got, tt.want) {
				t.Fatalf("args mismatch\n got: %#v\nwant: %#v", got, tt.want)
			}
		})
	}
}

func TestBuildDrvInstallArgsRejectsBadFilter(t *testing.T) {
	if _, err := buildDrvInstallArgs([]string{"MIGRATE", `E:\Windows`, "printer"}, testResolveDrvPath); err == nil {
		t.Fatal("expected bad filter to be rejected")
	}
}
