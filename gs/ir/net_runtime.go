package ir

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
)

func (g *Generator) useNetRuntime() {
	g.needNet = true
	if !g.hasHeader("gs_net_runtime.h") {
		g.cIncludes = append(g.cIncludes, `"gs_net_runtime.h"`)
		g.cHeaderCode = append(g.cHeaderCode, "static char HTTP_BODY_buf[1048576];\nstatic char* HTTP_BODY = HTTP_BODY_buf;\nstatic double HTTP_CODE = 0;\n")
	}
}

func (g *Generator) ensureNetRuntime() error {
	if !g.needNet {
		return nil
	}
	root, err := gsRootDir()
	if err != nil {
		return err
	}
	runtimeDir := filepath.Join(root, "runtime", "net")
	objDir := g.resultDir
	if objDir == "" || objDir == "." {
		objDir = os.TempDir()
	}
	objPath := filepath.Join(objDir, "gs_net_runtime.obj")
	clang := filepath.Join(g.llvmPath, "clang.exe")
	curlPrefix := firstExisting(
		`C:\msys64\ucrt64`,
		`C:\msys64\mingw64`,
	)
	if curlPrefix == "" {
		return fmt.Errorf("HTTP compiler runtime needs libcurl from MSYS2 UCRT64 or MINGW64")
	}
	includeDir := filepath.Join(curlPrefix, "include")
	libDir := filepath.Join(curlPrefix, "lib")
	g.extraCFlags = append(g.extraCFlags, "-I"+runtimeDir)
	runtimeC := filepath.Join(runtimeDir, "gs_net_runtime.c")
	llPath := filepath.Join(objDir, "gs_net_runtime.ll")
	llCmd := exec.Command(clang,
		"-S", "-emit-llvm", "-O2", "-Wno-everything",
		"-DCURL_STATICLIB",
		"-I"+runtimeDir,
		"-I"+includeDir,
		"-o", llPath,
		runtimeC,
	)
	llOut, err := llCmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("emit gs_net_runtime LLVM IR failed: %s", llOut)
	}
	cmd := exec.Command(clang,
		"-c", "-O2", "-Wno-everything",
		"-DCURL_STATICLIB",
		"-I"+runtimeDir,
		"-I"+includeDir,
		"-o", objPath,
		runtimeC,
	)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("compile gs_net_runtime failed: %s", out)
	}
	g.extraObjs = append(g.extraObjs, objPath)
	for _, lib := range curlStaticLibs(libDir) {
		g.extraLibs = append(g.extraLibs, lib)
	}
	return nil
}

func (g *Generator) hasHeader(name string) bool {
	quoted := `"` + name + `"`
	for _, inc := range g.cIncludes {
		if inc == quoted || inc == name {
			return true
		}
	}
	return false
}

func gsRootDir() (string, error) {
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		return "", fmt.Errorf("cannot locate gs runtime directory")
	}
	return filepath.Dir(filepath.Dir(file)), nil
}

func curlStaticLibs(libDir string) []string {
	names := []string{
		"libcurl.a", "libssl.a", "libcrypto.a", "libz.a", "libwldap32.a", "libws2_32.a", "libiphlpapi.a", "libbcrypt.a", "libadvapi32.a", "libcrypt32.a", "libsecur32.a", "libssh2.a", "libbrotlidec.a", "libbrotlicommon.a", "libzstd.a", "libnghttp2.a", "libngtcp2.a", "libngtcp2_crypto_ossl.a", "libgdi32.a", "libnghttp3.a", "libpsl.a", "libunistring.a", "libiconv.a", "libidn2.a", "libcharset.a",
	}
	seen := map[string]bool{}
	var out []string
	for _, name := range names {
		path := filepath.Join(libDir, name)
		if seen[path] {
			continue
		}
		if _, err := os.Stat(path); err == nil {
			out = append(out, path)
			seen[path] = true
		}
	}
	return out
}

func (g *Generator) ensureSysRuntime() error {
	root, err := gsRootDir()
	if err != nil {
		return err
	}
	objDir := g.resultDir
	if objDir == "" || objDir == "." {
		objDir = os.TempDir()
	}
	clang := filepath.Join(g.llvmPath, "clang.exe")
	msvcEnv := append(os.Environ(), buildMsvcEnv()...)

	// Compile gs_sys_runtime.c (registry, service, task, firewall)
	obj := filepath.Join(objDir, "gs_sys_runtime.obj")
	src := filepath.Join(root, "runtime", "llvm", "gs_sys_runtime.c")
	if _, err := os.Stat(src); err == nil {
		cmd := exec.Command(clang, "-c", "-O3", "-Wno-everything", "-o", obj, src)
		cmd.Env = msvcEnv
		if out, err := cmd.CombinedOutput(); err != nil {
			return fmt.Errorf("compile gs_sys_runtime failed: %s", out)
		}
		g.extraObjs = append(g.extraObjs, obj)
	}

	// Compile gs_extra_runtime.c (zip, regex, xml, vhd, aes, gpm, scripts, json_set)
	extraObj := filepath.Join(objDir, "gs_extra_runtime.obj")
	extraSrc := filepath.Join(root, "runtime", "llvm", "gs_extra_runtime.c")
	if _, err := os.Stat(extraSrc); err == nil {
		thirdparty := filepath.Join(root, "runtime", "thirdparty")
		cmd := exec.Command(clang, "-c", "-O3", "-Wno-everything",
			"-I"+thirdparty,
			"-I"+filepath.Join(thirdparty, "tiny-regex-c"),
			"-I"+filepath.Join(thirdparty, "tiny-AES-c"),
			"-o", extraObj, extraSrc)
		cmd.Env = msvcEnv
		if out, err := cmd.CombinedOutput(); err != nil {
			return fmt.Errorf("compile gs_extra_runtime failed: %s", out)
		}
		g.extraObjs = append(g.extraObjs, extraObj)
	}

	return nil
}
