package gs

import (
	"archive/tar"
	"archive/zip"
	"bytes"
	"compress/gzip"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/md5"
	"crypto/sha1"
	"crypto/sha256"
	"crypto/sha512"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"encoding/xml"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"

	"golang.org/x/sys/windows/registry"
)

func (i *Interp) cmdCalc(args []string) error {
	k, v := splitKV(args)
	if k == "" {
		return fmt.Errorf("CALC: need KEY=VALUE")
	}
	val := i.Expand(v)
	// If numeric, interpret as int and allow basic + - * /
	// Otherwise keep as string concat.
	if n, err := strconv.Atoi(val); err == nil {
		_ = n
		// TODO: proper expression eval with + - * /. For now just store.
	}
	i.SetVar(k, val)
	return nil
}

func (i *Interp) cmdStrl(args []string) error {
	k, v := splitKV(args)
	if k == "" {
		return fmt.Errorf("STRL: need KEY=VALUE")
	}
	i.SetVar(k, strconv.Itoa(len(v)))
	return nil
}

func (i *Interp) cmdPos(args []string, left bool) error {
	if len(args) < 1 {
		return fmt.Errorf("LPOS/RPOS: need KEY=STR,SUB")
	}
	k, rest := splitKV(args)
	restArgs := strings.Split(rest, ",")
	if len(restArgs) < 2 {
		return fmt.Errorf("LPOS/RPOS: need KEY=STR,SUB")
	}
	str := restArgs[0]
	sub := restArgs[1]
	n := -1
	if left {
		n = strings.Index(str, sub)
	} else {
		n = strings.LastIndex(str, sub)
	}
	i.SetVar(k, strconv.Itoa(n))
	return nil
}

func (i *Interp) cmdSubstr(args []string, typ rune) error {
	k, rest := splitKV(args)
	if k == "" || rest == "" {
		return fmt.Errorf("LSTR/RSTR/MSTR: need KEY=STR,ARGS...")
	}
	parts := strings.Split(rest, ",")
	if len(parts) < 1 {
		return fmt.Errorf("LSTR/RSTR/MSTR: need string arg")
	}
	s := parts[0]
	switch typ {
	case 'L':
		n, _ := strconv.Atoi(parts[1])
		if n >= len(s) {
			i.SetVar(k, s)
		} else {
			i.SetVar(k, s[:n])
		}
	case 'R':
		n, _ := strconv.Atoi(parts[1])
		if n >= len(s) {
			i.SetVar(k, s)
		} else {
			i.SetVar(k, s[len(s)-n:])
		}
	case 'M':
		from, _ := strconv.Atoi(parts[1])
		length, _ := strconv.Atoi(parts[2])
		if from >= len(s) {
			i.SetVar(k, "")
		} else {
			end := from + length
			if end > len(s) {
				end = len(s)
			}
			i.SetVar(k, s[from:end])
		}
	}
	return nil
}

func (i *Interp) cmdRegex(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("RGEX: need KEY=STR,PATTERN[,GROUP]")
	}
	k, rest := splitKV(args)
	parts := strings.Split(rest, ",")
	if len(parts) < 2 {
		return fmt.Errorf("RGEX: need STR,PATTERN")
	}
	re, err := regexp.Compile(parts[1])
	if err != nil {
		return fmt.Errorf("RGEX: bad pattern: %v", err)
	}
	groups := re.FindStringSubmatch(parts[0][len(k)+1:])
	groupIdx := 0
	if len(parts) >= 3 {
		groupIdx, _ = strconv.Atoi(parts[2])
	}
	if groupIdx < 0 || groupIdx >= len(groups) {
		i.SetVar(k, "")
		return nil
	}
	i.SetVar(k, groups[groupIdx])
	return nil
}

func (i *Interp) cmdRegexSub(args []string) error {
	if len(args) < 3 {
		return fmt.Errorf("RGSB: need KEY,STR,PATTERN,REPL")
	}
	k := args[0]
	s := i.Expand(args[1])
	pat := i.Expand(args[2])
	repl := ""
	if len(args) >= 4 {
		repl = i.Expand(args[3])
	}
	re, err := regexp.Compile(pat)
	if err != nil {
		return fmt.Errorf("RGSB: bad pattern: %v", err)
	}
	i.SetVar(k, re.ReplaceAllString(s, repl))
	return nil
}

func (i *Interp) cmdExec(ctx context.Context, args []string) error {
	if !i.caps.AllowExec {
		i.log("WARN", "EXEC: not allowed (no caps)")
		return nil
	}
	if len(args) == 0 {
		return nil
	}
	cmdStr := strings.Join(args, " ")
	i.log("INFO", fmt.Sprintf("EXEC: %s", cmdStr))
	c := exec.CommandContext(ctx, "cmd", "/c", cmdStr)
	c.Dir = i.scriptDir
	c.Stdout = i.stdout
	c.Stderr = i.stdout
	return c.Run()
}

func (i *Interp) cmdRunScripts(ctx context.Context, args []string) error {
	if !i.caps.AllowExec {
		i.log("WARN", "RUNS: not allowed (no caps)")
		return nil
	}
	dir := "."
	if len(args) > 0 && strings.TrimSpace(args[0]) != "" {
		dir = args[0]
	}
	scriptsDir := i.resolvePath(dir)
	entries := map[string][]string{}
	order := []string{".bat", ".cmd", ".ini", ".exe", ".reg", ".lua"}
	if err := filepath.Walk(scriptsDir, func(path string, info os.FileInfo, err error) error {
		if err != nil || info == nil || info.IsDir() {
			return nil
		}
		ext := strings.ToLower(filepath.Ext(path))
		for _, allowed := range order {
			if ext == allowed {
				entries[ext] = append(entries[ext], path)
				break
			}
		}
		return nil
	}); err != nil {
		return fmt.Errorf("RUNS: walk %s: %w", scriptsDir, err)
	}
	for _, ext := range order {
		scripts := entries[ext]
		sort.Strings(scripts)
		for _, script := range scripts {
			if err := i.runLegacyScript(ctx, script, ext); err != nil {
				return err
			}
		}
	}
	return nil
}

func (i *Interp) cmdPecmd(ctx context.Context, args []string) error {
	if !i.caps.AllowExec {
		i.log("WARN", "PECM: not allowed (no caps)")
		return nil
	}
	if len(args) < 2 {
		return fmt.Errorf("PECM: need LOAD/EXEC,PATH")
	}
	pecmd := i.findTool("pecmd.exe", i.caps.PecmdPath)
	if pecmd == "" {
		return fmt.Errorf("PECM: pecmd.exe not found")
	}
	op := strings.ToUpper(args[0])
	target := i.resolvePath(args[1])
	var c *exec.Cmd
	switch op {
	case "LOAD":
		c = exec.CommandContext(ctx, pecmd, "LOAD", target)
	case "EXEC":
		c = exec.CommandContext(ctx, pecmd, "EXEC", "!"+target)
	default:
		return fmt.Errorf("PECM: unknown operation %s", op)
	}
	c.Dir = filepath.Dir(target)
	c.Stdout = i.stdout
	c.Stderr = i.stdout
	return c.Run()
}

func (i *Interp) cmdWinXShell(ctx context.Context, args []string) error {
	if !i.caps.AllowExec {
		i.log("WARN", "WNSH: not allowed (no caps)")
		return nil
	}
	if len(args) < 1 {
		return fmt.Errorf("WNSH: need lua script path")
	}
	winxshell := i.findTool("winxshell.exe", i.caps.WinXShellPath)
	if winxshell == "" {
		return fmt.Errorf("WNSH: winxshell.exe not found")
	}
	script := i.resolvePath(args[0])
	c := exec.CommandContext(ctx, winxshell, "-script", filepath.Base(script))
	c.Dir = filepath.Dir(script)
	c.Stdout = i.stdout
	c.Stderr = i.stdout
	return c.Run()
}

func (i *Interp) cmdDrvInstall(ctx context.Context, args []string) error {
	if !i.caps.AllowExec {
		i.log("WARN", "DRVI: not allowed (no caps)")
		return nil
	}
	drvinstall := i.findToolAny([]string{"Drvinstall.exe", "drvinstall.exe", "drvindex.exe", "Drvindex.exe"}, i.caps.DrvInstallPath)
	if drvinstall == "" {
		return fmt.Errorf("DRVI: Drvinstall.exe not found")
	}
	argv, err := buildDrvInstallArgs(args, i.resolveDrvInstallPathArg)
	if err != nil {
		return err
	}
	i.log("INFO", "DRVI: "+strings.Join(argv, " "))
	c := exec.CommandContext(ctx, drvinstall, argv...)
	c.Dir = i.scriptDir
	c.Stdout = i.stdout
	c.Stderr = i.stdout
	return c.Run()
}

func buildDrvInstallArgs(args []string, resolve func(string) string) ([]string, error) {
	if len(args) == 0 || strings.TrimSpace(args[0]) == "" {
		return nil, fmt.Errorf("DRVI: need operation")
	}
	op := strings.ToUpper(strings.TrimSpace(args[0]))
	switch op {
	case "B", "BASE", "INSTALL":
		return drvInstallSourceArgs("-b", args[1:], resolve)
	case "T", "NODISPLAY", "NO-DISPLAY":
		return drvInstallSourceArgs("-t", args[1:], resolve)
	case "Y", "AUTO", "OFFLINE":
		return append([]string{"-y"}, drvProgressArg(args[1:])...), nil
	case "H", "AUTO-NODISPLAY", "OFFLINE-NODISPLAY":
		return append([]string{"-h"}, drvProgressArg(args[1:])...), nil
	case "IMPORT":
		if len(args) < 2 || strings.TrimSpace(args[1]) == "" {
			return nil, fmt.Errorf("DRVI IMPORT: need driver source")
		}
		out := []string{"-import", resolve(args[1])}
		if len(args) >= 3 && strings.TrimSpace(args[2]) != "" {
			out = append(out, resolve(args[2]))
		}
		return out, nil
	case "REMOVE":
		if len(args) < 2 || strings.TrimSpace(args[1]) == "" {
			return nil, fmt.Errorf("DRVI REMOVE: need oem inf name")
		}
		out := []string{"-remove", strings.TrimSpace(args[1])}
		if len(args) >= 3 && strings.TrimSpace(args[2]) != "" {
			out = append(out, resolve(args[2]))
		}
		return out, nil
	case "MIGRATE":
		if len(args) < 2 || strings.TrimSpace(args[1]) == "" {
			return nil, fmt.Errorf("DRVI MIGRATE: need offline image")
		}
		out := []string{"-migrate", resolve(args[1])}
		if len(args) >= 3 && strings.TrimSpace(args[2]) != "" {
			filter, err := sanitizeDrvFilter(args[2])
			if err != nil {
				return nil, err
			}
			out = append(out, "/filter:"+filter)
		}
		return out, nil
	case "BACKUP":
		if len(args) < 2 || strings.TrimSpace(args[1]) == "" {
			return nil, fmt.Errorf("DRVI BACKUP: need backup directory")
		}
		out := []string{"-backup", resolve(args[1])}
		if len(args) >= 3 && strings.TrimSpace(args[2]) != "" {
			out = append(out, resolve(args[2]))
		}
		if len(args) >= 4 && strings.TrimSpace(args[3]) != "" {
			filter, err := sanitizeDrvFilter(args[3])
			if err != nil {
				return nil, err
			}
			out = append(out, "/filter:"+filter)
		}
		return out, nil
	case "RAW":
		if len(args) < 2 {
			return nil, fmt.Errorf("DRVI RAW: need raw args")
		}
		return drvRawArgs(args[1:], resolve), nil
	default:
		return nil, fmt.Errorf("DRVI: unknown operation %s", op)
	}
}

func drvInstallSourceArgs(flag string, args []string, resolve func(string) string) ([]string, error) {
	out := []string{flag}
	if len(args) >= 1 && strings.TrimSpace(args[0]) != "" {
		out = append(out, resolve(args[0]))
	}
	if len(args) >= 2 && strings.TrimSpace(args[1]) != "" {
		out = append(out, "-p:"+strings.TrimSpace(args[1]))
	}
	if len(args) >= 3 && strings.TrimSpace(args[2]) != "" {
		out = append(out, "-config:"+resolve(args[2]))
	}
	out = append(out, drvProgressArg(args[3:])...)
	return out, nil
}

func drvProgressArg(args []string) []string {
	for _, arg := range args {
		switch strings.ToUpper(strings.TrimSpace(arg)) {
		case "NOPROGRESS", "NO-PROGRESS", "0", "FALSE":
			return nil
		}
	}
	return []string{"-Progress"}
}

func drvRawArgs(args []string, resolve func(string) string) []string {
	out := make([]string, 0, len(args))
	for _, arg := range args {
		arg = strings.TrimSpace(arg)
		if arg == "" {
			continue
		}
		switch {
		case strings.HasPrefix(arg, "-config:"):
			out = append(out, "-config:"+resolve(strings.TrimPrefix(arg, "-config:")))
		case strings.HasPrefix(arg, "/filter:"), strings.HasPrefix(arg, "-p:"):
			out = append(out, arg)
		default:
			out = append(out, resolve(arg))
		}
	}
	return out
}

func sanitizeDrvFilter(filter string) (string, error) {
	allowed := map[string]bool{
		"net": true, "display": true, "audio": true, "bluetooth": true, "system": true, "disk": true,
	}
	parts := strings.FieldsFunc(strings.ToLower(filter), func(r rune) bool {
		return r == ';' || r == ',' || r == ' '
	})
	var out []string
	for _, part := range parts {
		if part == "" {
			continue
		}
		if !allowed[part] {
			return "", fmt.Errorf("DRVI: unsupported filter category %s", part)
		}
		out = append(out, part)
	}
	if len(out) == 0 {
		return "", fmt.Errorf("DRVI: empty filter")
	}
	return strings.Join(out, ";"), nil
}

func (i *Interp) resolveDrvInstallPathArg(arg string) string {
	arg = strings.TrimSpace(arg)
	if arg == "" {
		return arg
	}
	if strings.HasPrefix(arg, "-") || strings.HasPrefix(arg, "/") {
		return arg
	}
	return i.resolvePath(arg)
}

func (i *Interp) cmdFile(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("FILE: need OP,PATH[,...]")
	}
	op := strings.ToUpper(args[0])
	target := i.resolvePath(args[1])
	switch op {
	case "COPY":
		if len(args) < 3 {
			return fmt.Errorf("FILE COPY: need SRC,DST")
		}
		dst := i.resolvePath(args[2])
		return copyFile(target, dst)
	case "MOVE":
		if len(args) < 3 {
			return fmt.Errorf("FILE MOVE: need SRC,DST")
		}
		dst := i.resolvePath(args[2])
		return os.Rename(target, dst)
	case "DEL":
		return os.Remove(target)
	case "READ":
		data, err := os.ReadFile(target)
		if err != nil {
			return err
		}
		if len(args) >= 3 {
			i.SetVar(args[2], string(data))
		} else {
			_, _ = i.stdout.Write(data)
		}
		return nil
	case "WRITE":
		if len(args) < 3 {
			return fmt.Errorf("FILE WRITE: need PATH,CONTENT")
		}
		return os.WriteFile(target, []byte(args[2]), 0644)
	case "APPEND":
		if len(args) < 3 {
			return fmt.Errorf("FILE APPEND: need PATH,CONTENT")
		}
		f, err := os.OpenFile(target, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err != nil {
			return err
		}
		defer f.Close()
		_, err = f.WriteString(args[2])
		return err
	}
	return fmt.Errorf("FILE: unknown operation %s", op)
}

func (i *Interp) cmdDir(args []string) error {
	if len(args) < 1 {
		return fmt.Errorf("FDIR: need OP[,PATH]")
	}
	op := strings.ToUpper(args[0])
	switch op {
	case "MAKE":
		path := "."
		if len(args) >= 2 {
			path = args[1]
		}
		return os.MkdirAll(i.resolvePath(path), 0755)
	case "DEL":
		if len(args) < 2 {
			return fmt.Errorf("FDIR DEL: need PATH")
		}
		return os.RemoveAll(filepath.Join(i.scriptDir, args[1]))
	case "LIST":
		if len(args) < 3 {
			return fmt.Errorf("FDIR LIST: need PATH,KEY")
		}
		entries, err := os.ReadDir(filepath.Join(i.scriptDir, args[1]))
		if err != nil {
			return err
		}
		var names []string
		for _, e := range entries {
			names = append(names, e.Name())
		}
		i.SetVar(args[2], strings.Join(names, "\n"))
		return nil
	}
	return fmt.Errorf("FDIR: unknown operation %s", op)
}

func (i *Interp) cmdLink(args []string) error {
	if !i.caps.AllowLink {
		i.log("WARN", "LINK: not allowed (no caps)")
		return nil
	}
	if len(args) < 3 {
		return fmt.Errorf("LINK: need TYPE,SRC,DST")
	}
	typ := strings.ToUpper(args[0])
	src := filepath.Join(i.scriptDir, args[1])
	dst := filepath.Join(i.scriptDir, args[2])
	switch typ {
	case "SYM":
		return os.Symlink(src, dst)
	case "HARD":
		return os.Link(src, dst)
	case "JUNC":
		// os.Symlink does junction on Windows for dirs.
		return os.Symlink(src, dst)
	}
	return fmt.Errorf("LINK: unknown type %s", typ)
}

func (i *Interp) cmdFext(args []string) error {
	k, path := splitKV(args)
	if k == "" || path == "" {
		return fmt.Errorf("FEXT: need KEY=PATH")
	}
	i.SetVar(k, filepath.Ext(path))
	return nil
}

func (i *Interp) cmdFdrv(args []string) error {
	k, path := splitKV(args)
	if k == "" || path == "" {
		return fmt.Errorf("FDRV: need KEY=PATH")
	}
	i.SetVar(k, filepath.VolumeName(path))
	return nil
}

func (i *Interp) cmdExist(args []string) error {
	k, path := splitKV(args)
	if k == "" || path == "" {
		return fmt.Errorf("EXIST: need KEY=PATH")
	}
	_, err := os.Stat(filepath.Join(i.scriptDir, path))
	if err == nil {
		i.SetVar(k, "1")
	} else {
		i.SetVar(k, "0")
	}
	return nil
}

func (i *Interp) cmdHash(args []string) error {
	// HASH K=algo,file_or_text  -- if file starts with '@' treat as text
	if len(args) < 1 {
		return fmt.Errorf("HASH: need KEY=ALGO,FILE_OR_TEXT")
	}
	k, rest := splitKV(args)
	parts := strings.Split(rest, ",")
	if len(parts) < 2 {
		return fmt.Errorf("HASH: need ALGO,FILE_OR_TEXT")
	}
	algo := strings.ToUpper(parts[0])
	src := parts[1]
	var data []byte
	if strings.HasPrefix(src, "@") {
		data = []byte(src[1:])
	} else {
		var err error
		data, err = os.ReadFile(filepath.Join(i.scriptDir, src))
		if err != nil {
			return err
		}
	}
	h := hashByName(algo, data)
	i.SetVar(k, h)
	return nil
}

func (i *Interp) cmdBase64(args []string) error {
	if len(args) < 1 {
		return fmt.Errorf("BASE: need KEY=ENC/DEC,TEXT")
	}
	k, rest := splitKV(args)
	parts := strings.Split(rest, ",")
	if len(parts) < 2 {
		return fmt.Errorf("BASE: need ENC/DEC,TEXT")
	}
	switch strings.TrimSpace(strings.ToUpper(parts[0])) {
	case "ENC":
		i.SetVar(k, base64.StdEncoding.EncodeToString([]byte(parts[1])))
	case "DEC":
		dec, err := base64.StdEncoding.DecodeString(parts[1])
		if err != nil {
			return fmt.Errorf("BASE DEC: %v", err)
		}
		i.SetVar(k, string(dec))
	default:
		return fmt.Errorf("BASE: need ENC or DEC")
	}
	return nil
}

func (i *Interp) cmdHex(args []string) error {
	if len(args) < 1 {
		return fmt.Errorf("HEXC: need KEY=ENC/DEC,TEXT")
	}
	k, rest := splitKV(args)
	parts := strings.Split(rest, ",")
	if len(parts) < 2 {
		return fmt.Errorf("HEXC: need ENC/DEC,TEXT")
	}
	switch strings.TrimSpace(strings.ToUpper(parts[0])) {
	case "ENC":
		i.SetVar(k, hex.EncodeToString([]byte(parts[1])))
	case "DEC":
		dec, err := hex.DecodeString(parts[1])
		if err != nil {
			return fmt.Errorf("HEXC DEC: %v", err)
		}
		i.SetVar(k, string(dec))
	default:
		return fmt.Errorf("HEXC: need ENC or DEC")
	}
	return nil
}

func (i *Interp) cmdAes(args []string) error {
	k, rest := splitKV(args)
	parts := strings.Split(rest, ",")
	if k == "" || len(parts) < 4 {
		return fmt.Errorf("AESC: need KEY=ENC/DEC,key,iv,text")
	}
	mode := strings.TrimSpace(strings.ToUpper(parts[0]))
	key, err := decodeCryptoBytes(parts[1])
	if err != nil {
		return fmt.Errorf("AESC key: %w", err)
	}
	iv, err := decodeCryptoBytes(parts[2])
	if err != nil {
		return fmt.Errorf("AESC iv: %w", err)
	}
	if len(iv) != aes.BlockSize {
		return fmt.Errorf("AESC: iv must be %d bytes", aes.BlockSize)
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return err
	}
	switch mode {
	case "ENC":
		plain := pkcs7Pad([]byte(parts[3]), aes.BlockSize)
		out := make([]byte, len(plain))
		cipher.NewCBCEncrypter(block, iv).CryptBlocks(out, plain)
		i.SetVar(k, base64.StdEncoding.EncodeToString(out))
	case "DEC":
		cipherText, err := base64.StdEncoding.DecodeString(parts[3])
		if err != nil {
			cipherText, err = hex.DecodeString(parts[3])
			if err != nil {
				return fmt.Errorf("AESC DEC: ciphertext must be base64 or hex")
			}
		}
		if len(cipherText)%aes.BlockSize != 0 {
			return fmt.Errorf("AESC DEC: ciphertext block size mismatch")
		}
		out := make([]byte, len(cipherText))
		cipher.NewCBCDecrypter(block, iv).CryptBlocks(out, cipherText)
		plain, err := pkcs7Unpad(out, aes.BlockSize)
		if err != nil {
			return err
		}
		i.SetVar(k, string(plain))
	default:
		return fmt.Errorf("AESC: need ENC or DEC")
	}
	return nil
}

func (i *Interp) cmdGpmInstall(ctx context.Context, args []string) error {
	if !i.caps.AllowGPM {
		i.log("WARN", "GPMI: not allowed (no caps)")
		return nil
	}
	return nil
}

func (i *Interp) cmdGpmUninstall(ctx context.Context, args []string) error {
	if !i.caps.AllowGPM {
		i.log("WARN", "GPMU: not allowed (no caps)")
		return nil
	}
	return nil
}

func (i *Interp) cmdGpmVersion(args []string) error {
	i.SetVar(args[0], "")
	return nil
}

func (i *Interp) cmdLogs(args []string) error {
	if len(args) < 1 {
		return nil
	}
	level := "INFO"
	msg := strings.Join(args, ",")
	if len(args) >= 2 {
		level = strings.ToUpper(args[0])
		msg = strings.Join(args[1:], ",")
	}
	i.log(level, msg)
	return nil
}

// --- stubs for extended modules ---

func (i *Interp) cmdVhdMount(ctx context.Context, args []string) error {
	if !i.caps.AllowVHD {
		i.log("WARN", "VHDM: not allowed (no caps)")
		return nil
	}
	if len(args) < 1 {
		return fmt.Errorf("VHDM: need PATH[,RO]")
	}
	ro := ""
	if len(args) >= 2 && strings.EqualFold(args[1], "RO") {
		ro = " readonly"
	}
	return i.diskpart(ctx, fmt.Sprintf("select vdisk file=\"%s\"\r\nattach vdisk%s\r\n", i.resolvePath(args[0]), ro))
}

func (i *Interp) cmdVhdUnmount(ctx context.Context, args []string) error {
	if !i.caps.AllowVHD {
		i.log("WARN", "VHDU: not allowed (no caps)")
		return nil
	}
	if len(args) < 1 {
		return fmt.Errorf("VHDU: need PATH")
	}
	return i.diskpart(ctx, fmt.Sprintf("select vdisk file=\"%s\"\r\ndetach vdisk\r\n", i.resolvePath(args[0])))
}

func (i *Interp) cmdVhdCreate(ctx context.Context, args []string) error {
	if !i.caps.AllowVHD {
		i.log("WARN", "VHDC: not allowed (no caps)")
		return nil
	}
	if len(args) < 3 {
		return fmt.Errorf("VHDC: need PATH,MB,FIXED|DYNAMIC")
	}
	typ := strings.ToLower(args[2])
	if typ != "fixed" && typ != "dynamic" {
		return fmt.Errorf("VHDC: type must be FIXED or DYNAMIC")
	}
	return i.diskpart(ctx, fmt.Sprintf("create vdisk file=\"%s\" maximum=%s type=%s\r\n", i.resolvePath(args[0]), args[1], typ))
}

func (i *Interp) cmdJsonRead(args []string) error {
	k, rest := splitKV(args)
	if k == "" || rest == "" {
		return fmt.Errorf("JSON: need KEY=FILE_OR_TEXT,PATH")
	}
	parts := strings.Split(rest, ",")
	if len(parts) < 2 {
		return fmt.Errorf("JSON: need source and path")
	}
	src := strings.TrimSpace(parts[0])
	jsonPath := strings.TrimSpace(parts[1])
	var raw interface{}
	if strings.HasPrefix(src, "@") {
		if err := json.Unmarshal([]byte(src[1:]), &raw); err != nil {
			return fmt.Errorf("JSON: parse inline: %v", err)
		}
	} else {
		data, err := os.ReadFile(filepath.Join(i.scriptDir, src))
		if err != nil {
			return fmt.Errorf("JSON: read %s: %s", src, userFileError(err))
		}
		if err := json.Unmarshal(data, &raw); err != nil {
			return fmt.Errorf("JSON: parse %s: %v", src, err)
		}
	}
	val, err := jsonPathLookup(raw, jsonPath)
	if err != nil {
		return err
	}
	i.SetVar(k, val)
	return nil
}

func (i *Interp) cmdJsonSet(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("JSNS: need FILE,PATH,VALUE or FILE,PATH=VALUE")
	}
	file := args[0]
	jsonPath := ""
	value := ""
	if len(args) >= 3 {
		jsonPath = args[1]
		value = strings.Join(args[2:], ",")
	} else {
		idx := strings.Index(args[1], "=")
		if idx < 0 {
			return fmt.Errorf("JSNS: need FILE,PATH=VALUE")
		}
		jsonPath = args[1][:idx]
		value = args[1][idx+1:]
	}
	path := i.resolvePath(file)
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	var raw interface{}
	if len(data) == 0 {
		raw = map[string]interface{}{}
	} else if err := json.Unmarshal(data, &raw); err != nil {
		return err
	}
	if err := jsonPathSet(&raw, jsonPath, value); err != nil {
		return err
	}
	out, err := json.MarshalIndent(raw, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, out, 0644)
}

func (i *Interp) cmdJsonLen(args []string) error {
	k, rest := splitKV(args)
	if k == "" || rest == "" {
		return fmt.Errorf("JSNL: need KEY=FILE,PATH")
	}
	parts := strings.SplitN(rest, ",", 2)
	if len(parts) < 2 {
		return fmt.Errorf("JSNL: need FILE,PATH")
	}
	src := strings.TrimSpace(parts[0])
	jsonPath := strings.TrimSpace(parts[1])
	var raw interface{}
	if strings.HasPrefix(src, "@") {
		if err := json.Unmarshal([]byte(src[1:]), &raw); err != nil {
			return fmt.Errorf("JSNL: parse inline: %v", err)
		}
	} else {
		data, err := os.ReadFile(filepath.Join(i.scriptDir, src))
		if err != nil {
			return fmt.Errorf("JSNL: read %s: %s", src, userFileError(err))
		}
		if err := json.Unmarshal(data, &raw); err != nil {
			return fmt.Errorf("JSNL: parse %s: %v", src, err)
		}
	}
	arr, err := jsonPathLookupArray(raw, jsonPath)
	if err != nil {
		return err
	}
	i.SetVar(k, strconv.Itoa(len(arr)))
	return nil
}

func userFileError(err error) string {
	if err == nil {
		return ""
	}
	if os.IsNotExist(err) {
		return "file not found"
	}
	if os.IsPermission(err) {
		return "permission denied"
	}
	return err.Error()
}

func (i *Interp) cmdXmlRead(args []string) error {
	k, rest := splitKV(args)
	if k == "" || rest == "" {
		return fmt.Errorf("XMLR: need KEY=FILE,XPATH")
	}
	parts := strings.SplitN(rest, ",", 2)
	if len(parts) < 2 {
		return fmt.Errorf("XMLR: need FILE,XPATH")
	}
	data, err := os.ReadFile(i.resolvePath(parts[0]))
	if err != nil {
		return err
	}
	val, err := xmlSimpleRead(data, parts[1])
	if err != nil {
		return err
	}
	i.SetVar(k, val)
	return nil
}

func (i *Interp) cmdXmlWrite(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("XMLW: need FILE,XPATH=VALUE")
	}
	file := args[0]
	idx := strings.Index(args[1], "=")
	if idx < 0 {
		return fmt.Errorf("XMLW: need XPATH=VALUE")
	}
	xpath := args[1][:idx]
	value := args[1][idx+1:]
	if len(args) > 2 {
		value = value + "," + strings.Join(args[2:], ",")
	}
	path := i.resolvePath(file)
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	out, err := xmlSimpleWrite(data, xpath, value)
	if err != nil {
		return err
	}
	return os.WriteFile(path, out, 0644)
}

func (i *Interp) cmdHttp(ctx context.Context, args []string) error {
	if !i.caps.AllowHTTP {
		i.log("WARN", "HTTP: not allowed (no caps)")
		return nil
	}
	if len(args) < 2 {
		return fmt.Errorf("HTTP: need METHOD,URL[,BODY]")
	}
	method := strings.ToUpper(args[0])
	url := args[1]
	body := ""
	if len(args) >= 3 {
		body = strings.Join(args[2:], ",")
	}
	req, err := http.NewRequestWithContext(ctx, method, url, strings.NewReader(body))
	if err != nil {
		return err
	}
	if body != "" {
		req.Header.Set("Content-Type", "application/octet-stream")
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return err
	}
	i.SetVar("HTTP_CODE", strconv.Itoa(resp.StatusCode))
	i.SetVar("HTTP_BODY", string(data))
	return nil
}

func (i *Interp) cmdDownload(ctx context.Context, args []string) error {
	if !i.caps.AllowHTTP {
		i.log("WARN", "DOWN: not allowed (no caps)")
		return nil
	}
	if len(args) < 2 {
		return fmt.Errorf("DOWN: need URL,FILE")
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, args[0], nil)
	if err != nil {
		return err
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("DOWN: server returned %d", resp.StatusCode)
	}
	dst := i.resolvePath(args[1])
	if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
		return err
	}
	f, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = io.Copy(f, resp.Body)
	return err
}

func (i *Interp) cmdUpload(ctx context.Context, args []string) error {
	if !i.caps.AllowHTTP {
		i.log("WARN", "UPLD: not allowed (no caps)")
		return nil
	}
	if len(args) < 2 {
		return fmt.Errorf("UPLD: need FILE,URL")
	}
	filePath := i.resolvePath(args[0])
	var body bytes.Buffer
	mw := multipart.NewWriter(&body)
	part, err := mw.CreateFormFile("file", filepath.Base(filePath))
	if err != nil {
		return err
	}
	f, err := os.Open(filePath)
	if err != nil {
		return err
	}
	if _, err := io.Copy(part, f); err != nil {
		f.Close()
		return err
	}
	f.Close()
	if err := mw.Close(); err != nil {
		return err
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, args[1], &body)
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", mw.FormDataContentType())
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	data, _ := io.ReadAll(resp.Body)
	i.SetVar("HTTP_CODE", strconv.Itoa(resp.StatusCode))
	i.SetVar("HTTP_BODY", string(data))
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("UPLD: server returned %d", resp.StatusCode)
	}
	return nil
}

func (i *Interp) cmdZipExtract(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("ZIPX: need ARCHIVE,DEST_DIR")
	}
	return unzipToDir(i.resolvePath(args[0]), i.resolvePath(args[1]))
}

func (i *Interp) cmdZipCreate(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("ZIPC: need ARCHIVE,SRC_DIR")
	}
	return zipDir(i.resolvePath(args[1]), i.resolvePath(args[0]))
}

func (i *Interp) cmdTarExtract(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("TARX: need ARCHIVE,DEST_DIR")
	}
	return untarToDir(i.resolvePath(args[0]), i.resolvePath(args[1]))
}

func (i *Interp) cmdService(ctx context.Context, args []string) error {
	if !i.caps.AllowService {
		i.log("WARN", "SERV: not allowed (no caps)")
		return nil
	}
	if len(args) < 2 {
		return fmt.Errorf("SERV: need START/STOP/RESTART/STATUS,NAME")
	}
	op, name := strings.ToUpper(args[0]), args[1]
	switch op {
	case "START":
		return i.runShell(ctx, "sc start "+quoteCmd(name))
	case "STOP":
		return i.runShell(ctx, "sc stop "+quoteCmd(name))
	case "RESTART":
		if err := i.runShell(ctx, "sc stop "+quoteCmd(name)); err != nil {
			i.log("WARN", "SERV RESTART stop: "+err.Error())
		}
		return i.runShell(ctx, "sc start "+quoteCmd(name))
	case "STATUS":
		return i.runShell(ctx, "sc query "+quoteCmd(name))
	default:
		return fmt.Errorf("SERV: unknown operation %s", op)
	}
}

func (i *Interp) cmdTask(ctx context.Context, args []string) error {
	if !i.caps.AllowScheduled {
		i.log("WARN", "TASK: not allowed (no caps)")
		return nil
	}
	if len(args) < 2 {
		return fmt.Errorf("TASK: need RUN/DEL/QUERY/CREATE,NAME[,...]")
	}
	op, name := strings.ToUpper(args[0]), args[1]
	switch op {
	case "RUN":
		return i.runShell(ctx, "schtasks /Run /TN "+quoteCmd(name))
	case "DEL":
		return i.runShell(ctx, "schtasks /Delete /F /TN "+quoteCmd(name))
	case "QUERY", "STATUS":
		return i.runShell(ctx, "schtasks /Query /TN "+quoteCmd(name))
	case "CREATE":
		if len(args) < 4 {
			return fmt.Errorf("TASK CREATE: need NAME,TRIGGER,COMMAND")
		}
		return i.runShell(ctx, "schtasks /Create /F /TN "+quoteCmd(name)+" /SC "+quoteCmd(args[2])+" /TR "+quoteCmd(strings.Join(args[3:], ",")))
	default:
		return fmt.Errorf("TASK: unknown operation %s", op)
	}
}

func (i *Interp) cmdFirewall(ctx context.Context, args []string) error {
	if !i.caps.AllowFirewall {
		i.log("WARN", "FWAL: not allowed (no caps)")
		return nil
	}
	if len(args) < 2 {
		return fmt.Errorf("FWAL: need ADD/DEL,rule args")
	}
	op := strings.ToUpper(args[0])
	rest := strings.Join(args[1:], ",")
	switch op {
	case "ADD":
		return i.runShell(ctx, "netsh advfirewall firewall add rule "+rest)
	case "DEL", "DELETE":
		return i.runShell(ctx, "netsh advfirewall firewall delete rule "+rest)
	default:
		return fmt.Errorf("FWAL: unknown operation %s", op)
	}
}

func (i *Interp) cmdRegistry(args []string) error {
	if !i.caps.AllowRegistry {
		i.log("WARN", "REGI: not allowed (no caps)")
		return nil
	}
	if len(args) < 2 {
		return fmt.Errorf("REGI: need GET/SET/DEL,ROOT\\PATH[,NAME[,VALUE[,TYPE]]]")
	}
	op := strings.ToUpper(args[0])
	root, subkey, err := parseRegistryPath(args[1])
	if err != nil {
		return err
	}
	switch op {
	case "GET":
		if len(args) < 4 {
			return fmt.Errorf("REGI GET: need PATH,NAME,VAR")
		}
		k, err := registry.OpenKey(root, subkey, registry.QUERY_VALUE)
		if err != nil {
			return err
		}
		defer k.Close()
		val, _, err := k.GetStringValue(args[2])
		if err != nil {
			return err
		}
		i.SetVar(args[3], val)
	case "SET":
		if len(args) < 5 {
			return fmt.Errorf("REGI SET: need PATH,NAME,VALUE,TYPE")
		}
		k, _, err := registry.CreateKey(root, subkey, registry.SET_VALUE)
		if err != nil {
			return err
		}
		defer k.Close()
		name, val, typ := args[2], args[3], strings.ToUpper(args[4])
		switch typ {
		case "SZ", "STRING", "REG_SZ":
			return k.SetStringValue(name, val)
		case "DWORD", "REG_DWORD":
			n, err := strconv.ParseUint(val, 0, 32)
			if err != nil {
				return err
			}
			return k.SetDWordValue(name, uint32(n))
		case "QWORD", "REG_QWORD":
			n, err := strconv.ParseUint(val, 0, 64)
			if err != nil {
				return err
			}
			return k.SetQWordValue(name, n)
		default:
			return fmt.Errorf("REGI SET: unsupported type %s", typ)
		}
	case "DEL":
		if len(args) >= 3 && strings.TrimSpace(args[2]) != "" {
			k, err := registry.OpenKey(root, subkey, registry.SET_VALUE)
			if err != nil {
				return err
			}
			defer k.Close()
			return k.DeleteValue(args[2])
		}
		return registry.DeleteKey(root, subkey)
	default:
		return fmt.Errorf("REGI: unknown operation %s", op)
	}
	return nil
}

// --- helpers ---

func (i *Interp) resolvePath(path string) string {
	path = strings.TrimSpace(path)
	if path == "" {
		return i.scriptDir
	}
	if filepath.IsAbs(path) {
		return filepath.Clean(path)
	}
	return filepath.Join(i.scriptDir, path)
}

func (i *Interp) findTool(name string, explicit string) string {
	if explicit != "" {
		if _, err := os.Stat(explicit); err == nil {
			return explicit
		}
	}
	if i.caps.TempResDir != "" {
		candidate := filepath.Join(i.caps.TempResDir, "res", name)
		if _, err := os.Stat(candidate); err == nil {
			return candidate
		}
		if candidate := findFileCaseInsensitive(filepath.Join(i.caps.TempResDir, "res"), name); candidate != "" {
			return candidate
		}
	}
	if found, err := exec.LookPath(name); err == nil {
		return found
	}
	return ""
}

func (i *Interp) findToolAny(names []string, explicit string) string {
	for _, name := range names {
		if found := i.findTool(name, explicit); found != "" {
			return found
		}
	}
	return ""
}

func findFileCaseInsensitive(dir string, name string) string {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return ""
	}
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		if strings.EqualFold(entry.Name(), name) {
			return filepath.Join(dir, entry.Name())
		}
	}
	return ""
}

func (i *Interp) runLegacyScript(ctx context.Context, scriptPath string, ext string) error {
	i.log("INFO", "RUNS: "+scriptPath)
	workingDir := filepath.Dir(scriptPath)
	var c *exec.Cmd
	switch ext {
	case ".bat", ".cmd":
		abs, err := filepath.Abs(scriptPath)
		if err != nil {
			return err
		}
		c = exec.CommandContext(ctx, "cmd.exe", "/c", "chcp", "65001", ">nul", "&&", "cd", "/d", workingDir, "&&", abs)
	case ".exe":
		pecmd := i.findTool("pecmd.exe", i.caps.PecmdPath)
		if pecmd != "" {
			c = exec.CommandContext(ctx, pecmd, "EXEC", "!"+scriptPath)
		} else {
			c = exec.CommandContext(ctx, scriptPath)
		}
	case ".ini":
		pecmd := i.findTool("pecmd.exe", i.caps.PecmdPath)
		if pecmd == "" {
			i.log("WARN", "RUNS: skip .ini, pecmd.exe not found: "+scriptPath)
			return nil
		}
		c = exec.CommandContext(ctx, pecmd, "LOAD", scriptPath)
	case ".reg":
		regedit, err := exec.LookPath("regedit.exe")
		if err != nil {
			i.log("WARN", "RUNS: skip .reg, regedit.exe not found: "+scriptPath)
			return nil
		}
		c = exec.CommandContext(ctx, regedit, "/s", scriptPath)
	case ".lua":
		winxshell := i.findTool("winxshell.exe", i.caps.WinXShellPath)
		if winxshell == "" {
			i.log("WARN", "RUNS: skip .lua, winxshell.exe not found: "+scriptPath)
			return nil
		}
		c = exec.CommandContext(ctx, winxshell, "-script", filepath.Base(scriptPath))
	default:
		return nil
	}
	c.Dir = workingDir
	c.Stdout = i.stdout
	c.Stderr = i.stdout
	return c.Run()
}

func copyFile(src, dst string) error {
	data, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
		return err
	}
	return os.WriteFile(dst, data, 0644)
}

func unzipToDir(archivePath, destDir string) error {
	r, err := zip.OpenReader(archivePath)
	if err != nil {
		return err
	}
	defer r.Close()
	cleanDest := filepath.Clean(destDir)
	for _, f := range r.File {
		name := filepath.Clean(filepath.FromSlash(f.Name))
		if strings.HasPrefix(name, "..") || filepath.IsAbs(name) {
			return fmt.Errorf("ZIPX: unsafe path %s", f.Name)
		}
		target := filepath.Join(cleanDest, name)
		if !strings.HasPrefix(target, cleanDest+string(os.PathSeparator)) && target != cleanDest {
			return fmt.Errorf("ZIPX: path escapes dest %s", f.Name)
		}
		if f.FileInfo().IsDir() {
			if err := os.MkdirAll(target, f.Mode()); err != nil {
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(target), 0755); err != nil {
			return err
		}
		rc, err := f.Open()
		if err != nil {
			return err
		}
		out, err := os.OpenFile(target, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, f.Mode())
		if err != nil {
			rc.Close()
			return err
		}
		_, copyErr := io.Copy(out, rc)
		closeErr := out.Close()
		rc.Close()
		if copyErr != nil {
			return copyErr
		}
		if closeErr != nil {
			return closeErr
		}
	}
	return nil
}

func zipDir(srcDir, archivePath string) error {
	if err := os.MkdirAll(filepath.Dir(archivePath), 0755); err != nil {
		return err
	}
	out, err := os.Create(archivePath)
	if err != nil {
		return err
	}
	defer out.Close()
	zw := zip.NewWriter(out)
	defer zw.Close()
	root := filepath.Clean(srcDir)
	return filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil || info == nil {
			return err
		}
		if info.IsDir() {
			return nil
		}
		rel, err := filepath.Rel(root, path)
		if err != nil {
			return err
		}
		rel = filepath.ToSlash(rel)
		h, err := zip.FileInfoHeader(info)
		if err != nil {
			return err
		}
		h.Name = rel
		h.Method = zip.Deflate
		w, err := zw.CreateHeader(h)
		if err != nil {
			return err
		}
		in, err := os.Open(path)
		if err != nil {
			return err
		}
		defer in.Close()
		_, err = io.Copy(w, in)
		return err
	})
}

func untarToDir(archivePath, destDir string) error {
	f, err := os.Open(archivePath)
	if err != nil {
		return err
	}
	defer f.Close()
	var reader io.Reader = f
	if strings.HasSuffix(strings.ToLower(archivePath), ".gz") || strings.HasSuffix(strings.ToLower(archivePath), ".tgz") {
		gz, err := gzip.NewReader(f)
		if err != nil {
			return err
		}
		defer gz.Close()
		reader = gz
	}
	tr := tar.NewReader(reader)
	cleanDest := filepath.Clean(destDir)
	for {
		h, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}
		name := filepath.Clean(filepath.FromSlash(h.Name))
		if strings.HasPrefix(name, "..") || filepath.IsAbs(name) {
			return fmt.Errorf("TARX: unsafe path %s", h.Name)
		}
		target := filepath.Join(cleanDest, name)
		if h.FileInfo().IsDir() {
			if err := os.MkdirAll(target, h.FileInfo().Mode()); err != nil {
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(target), 0755); err != nil {
			return err
		}
		out, err := os.OpenFile(target, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, h.FileInfo().Mode())
		if err != nil {
			return err
		}
		_, copyErr := io.Copy(out, tr)
		closeErr := out.Close()
		if copyErr != nil {
			return copyErr
		}
		if closeErr != nil {
			return closeErr
		}
	}
	return nil
}

func decodeCryptoBytes(s string) ([]byte, error) {
	if strings.HasPrefix(strings.ToLower(s), "hex:") {
		return hex.DecodeString(s[4:])
	}
	if strings.HasPrefix(strings.ToLower(s), "b64:") {
		return base64.StdEncoding.DecodeString(s[4:])
	}
	if dec, err := hex.DecodeString(s); err == nil && len(dec) > 0 {
		return dec, nil
	}
	return []byte(s), nil
}

func pkcs7Pad(data []byte, blockSize int) []byte {
	pad := blockSize - len(data)%blockSize
	out := make([]byte, len(data)+pad)
	copy(out, data)
	for j := len(data); j < len(out); j++ {
		out[j] = byte(pad)
	}
	return out
}

func pkcs7Unpad(data []byte, blockSize int) ([]byte, error) {
	if len(data) == 0 || len(data)%blockSize != 0 {
		return nil, fmt.Errorf("PKCS7: invalid data length")
	}
	pad := int(data[len(data)-1])
	if pad == 0 || pad > blockSize || pad > len(data) {
		return nil, fmt.Errorf("PKCS7: invalid padding")
	}
	for _, b := range data[len(data)-pad:] {
		if int(b) != pad {
			return nil, fmt.Errorf("PKCS7: invalid padding")
		}
	}
	return data[:len(data)-pad], nil
}

func quoteCmd(s string) string {
	return `"` + strings.ReplaceAll(s, `"`, `\"`) + `"`
}

func (i *Interp) runShell(ctx context.Context, cmdLine string) error {
	c := exec.CommandContext(ctx, "cmd.exe", "/c", cmdLine)
	c.Dir = i.scriptDir
	c.Stdout = i.stdout
	c.Stderr = i.stdout
	return c.Run()
}

func (i *Interp) diskpart(ctx context.Context, script string) error {
	f, err := os.CreateTemp("", "gs-diskpart-*.txt")
	if err != nil {
		return err
	}
	path := f.Name()
	if _, err := f.WriteString(script); err != nil {
		f.Close()
		os.Remove(path)
		return err
	}
	f.Close()
	defer os.Remove(path)
	c := exec.CommandContext(ctx, "diskpart.exe", "/s", path)
	c.Stdout = i.stdout
	c.Stderr = i.stdout
	return c.Run()
}

func parseRegistryPath(path string) (registry.Key, string, error) {
	p := strings.Trim(path, `\`)
	parts := strings.SplitN(p, `\`, 2)
	if len(parts) < 2 {
		return 0, "", fmt.Errorf("REGI: path must be ROOT\\SubKey")
	}
	rootName := strings.ToUpper(parts[0])
	subkey := parts[1]
	switch rootName {
	case "HKCU", "HKEY_CURRENT_USER":
		return registry.CURRENT_USER, subkey, nil
	case "HKLM", "HKEY_LOCAL_MACHINE":
		return registry.LOCAL_MACHINE, subkey, nil
	case "HKCR", "HKEY_CLASSES_ROOT":
		return registry.CLASSES_ROOT, subkey, nil
	case "HKU", "HKEY_USERS":
		return registry.USERS, subkey, nil
	case "HKCC", "HKEY_CURRENT_CONFIG":
		return registry.CURRENT_CONFIG, subkey, nil
	default:
		return 0, "", fmt.Errorf("REGI: unknown root %s", parts[0])
	}
}

func jsonPathSet(root *interface{}, path string, value string) error {
	if !strings.HasPrefix(path, "$") {
		return fmt.Errorf("JSON path must start with $")
	}
	parts := strings.Split(strings.TrimPrefix(path[1:], "."), ".")
	if len(parts) == 0 {
		return fmt.Errorf("JSON path empty")
	}
	m, ok := (*root).(map[string]interface{})
	if !ok {
		m = map[string]interface{}{}
		*root = m
	}
	for idx, part := range parts {
		if part == "" {
			continue
		}
		if idx == len(parts)-1 {
			m[part] = parseJsonScalar(value)
			return nil
		}
		next, ok := m[part].(map[string]interface{})
		if !ok {
			next = map[string]interface{}{}
			m[part] = next
		}
		m = next
	}
	return nil
}

func parseJsonScalar(value string) interface{} {
	v := strings.TrimSpace(value)
	if v == "true" {
		return true
	}
	if v == "false" {
		return false
	}
	if v == "null" {
		return nil
	}
	if n, err := strconv.ParseInt(v, 10, 64); err == nil {
		return n
	}
	if f, err := strconv.ParseFloat(v, 64); err == nil && strings.Contains(v, ".") {
		return f
	}
	return value
}

func xmlSimpleRead(data []byte, xpath string) (string, error) {
	want := splitXPath(xpath)
	if len(want) == 0 {
		return "", fmt.Errorf("XMLR: empty xpath")
	}
	dec := xml.NewDecoder(bytes.NewReader(data))
	var stack []string
	for {
		tok, err := dec.Token()
		if err == io.EOF {
			break
		}
		if err != nil {
			return "", err
		}
		switch t := tok.(type) {
		case xml.StartElement:
			stack = append(stack, t.Name.Local)
			if equalStringSlices(stack, want) {
				var text string
				if err := dec.DecodeElement(&text, &t); err != nil {
					return "", err
				}
				return text, nil
			}
		case xml.EndElement:
			if len(stack) > 0 {
				stack = stack[:len(stack)-1]
			}
		}
	}
	return "", fmt.Errorf("XMLR: xpath not found %s", xpath)
}

func xmlSimpleWrite(data []byte, xpath string, value string) ([]byte, error) {
	parts := splitXPath(xpath)
	if len(parts) == 0 {
		return nil, fmt.Errorf("XMLW: empty xpath")
	}
	leaf := parts[len(parts)-1]
	re := regexp.MustCompile(`(?s)<` + regexp.QuoteMeta(leaf) + `([^>]*)>.*?</` + regexp.QuoteMeta(leaf) + `>`)
	repl := `<` + leaf + `$1>` + value + `</` + leaf + `>`
	out := re.ReplaceAll(data, []byte(repl))
	if bytes.Equal(out, data) {
		return nil, fmt.Errorf("XMLW: element not found %s", leaf)
	}
	return out, nil
}

func splitXPath(xpath string) []string {
	x := strings.Trim(xpath, "/ ")
	if strings.HasPrefix(x, "$") {
		x = strings.TrimPrefix(strings.TrimPrefix(x, "$"), ".")
	}
	if x == "" {
		return nil
	}
	return strings.FieldsFunc(x, func(r rune) bool { return r == '/' || r == '.' })
}

func equalStringSlices(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	for idx := range a {
		if a[idx] != b[idx] {
			return false
		}
	}
	return true
}

func hashByName(algo string, data []byte) string {
	switch algo {
	case "MD5":
		return fmt.Sprintf("%x", md5Hash(data))
	case "SHA1":
		return fmt.Sprintf("%x", sha1Hash(data))
	case "SHA256":
		return fmt.Sprintf("%x", sha256Hash(data))
	case "SHA512":
		return fmt.Sprintf("%x", sha512Hash(data))
	default:
		return ""
	}
}

// Compile-free hash helpers using crypto Go stdlib
func md5Hash(d []byte) []byte    { h := md5.Sum(d); return h[:] }
func sha1Hash(d []byte) []byte   { h := sha1.Sum(d); return h[:] }
func sha256Hash(d []byte) []byte { h := sha256.Sum256(d); return h[:] }
func sha512Hash(d []byte) []byte { h := sha512.Sum512(d); return h[:] }

func jsonPathLookup(root interface{}, path string) (string, error) {
	// Simple $.key.subkey or $.key[0].subkey path
	if !strings.HasPrefix(path, "$") {
		return "", fmt.Errorf("JSON path must start with $")
	}
	current := root
	parts := strings.Split(strings.TrimPrefix(path[1:], "."), ".")
	for _, part := range parts {
		if part == "" {
			continue
		}
		bracket := strings.Index(part, "[")
		key := part
		idx := -1
		if bracket >= 0 {
			key = part[:bracket]
			end := strings.Index(part, "]")
			if end > bracket+1 {
				idx, _ = strconv.Atoi(part[bracket+1 : end])
			}
		}
		obj, ok := current.(map[string]interface{})
		if ok {
			if key != "" {
				current = obj[key]
			} else if idx >= 0 {
				return "", fmt.Errorf("cannot index object with %d", idx)
			}
		} else {
			arr, ok := current.([]interface{})
			if ok {
				if idx >= 0 {
					current = arr[idx]
				} else {
					return "", fmt.Errorf("cannot key array with %q", key)
				}
			} else {
				return "", fmt.Errorf("path %q not found", part)
			}
		}
	}
	switch v := current.(type) {
	case string:
		return v, nil
	case float64:
		return strconv.FormatFloat(v, 'f', -1, 64), nil
	case bool:
		return fmt.Sprintf("%t", v), nil
	case nil:
		return "", nil
	default:
		b, _ := json.Marshal(v)
		return string(b), nil
	}
}

func jsonPathLookupArray(root interface{}, path string) ([]interface{}, error) {
	current := root
	if !strings.HasPrefix(path, "$") {
		return nil, fmt.Errorf("JSON path must start with $")
	}
	parts := strings.Split(strings.TrimPrefix(path[1:], "."), ".")
	for _, part := range parts {
		if part == "" {
			continue
		}
		bracket := strings.Index(part, "[")
		key := part
		idx := -1
		if bracket >= 0 {
			key = part[:bracket]
			end := strings.Index(part, "]")
			if end > bracket+1 {
				idx, _ = strconv.Atoi(part[bracket+1 : end])
			}
		}
		if key != "" {
			obj, ok := current.(map[string]interface{})
			if !ok {
				return nil, fmt.Errorf("path key %q not in object", key)
			}
			current = obj[key]
		}
		if idx >= 0 {
			arr, ok := current.([]interface{})
			if !ok {
				return nil, fmt.Errorf("path index %d not in array", idx)
			}
			if idx >= len(arr) {
				return nil, fmt.Errorf("array index %d out of bounds", idx)
			}
			current = arr[idx]
		}
	}
	arr, ok := current.([]interface{})
	if !ok {
		return nil, fmt.Errorf("path does not resolve to array")
	}
	return arr, nil
}
