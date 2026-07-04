package ir

import (
	"crypto/md5"
	"crypto/sha1"
	"crypto/sha256"
	"crypto/sha512"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/SECTL/GPM/gs"
)

func (g *Generator) genCall(args []string) error {
	if len(args) == 0 || strings.TrimSpace(args[0]) == "" {
		return fmt.Errorf("CALL: missing name")
	}
	name := strings.ToUpper(strings.TrimSpace(args[0]))
	body, ok := g.prog.Subs[name]
	if !ok {
		return fmt.Errorf("CALL: unknown FUNC %s", args[0])
	}
	if g.callStack[name] {
		return fmt.Errorf("CALL: recursive FUNC %s", args[0])
	}
	g.callStack[name] = true
	defer delete(g.callStack, name)
	return g.genStatements(body)
}

func (g *Generator) genFile(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("FILE: need OP,PATH[,...]")
	}
	op := strings.ToUpper(strings.TrimSpace(args[0]))
	switch op {
	case "COPY":
		if len(args) < 3 {
			return fmt.Errorf("FILE COPY: need SRC,DST")
		}
		fmt.Fprintf(&g.code, "    gs_file_copy(%s, %s);\n", g.cValue(args[1], TypeString), g.cValue(args[2], TypeString))
	case "MOVE":
		if len(args) < 3 {
			return fmt.Errorf("FILE MOVE: need SRC,DST")
		}
		fmt.Fprintf(&g.code, "    gs_file_move(%s, %s);\n", g.cValue(args[1], TypeString), g.cValue(args[2], TypeString))
	case "DEL":
		fmt.Fprintf(&g.code, "    gs_file_del(%s);\n", g.cValue(args[1], TypeString))
	case "READ":
		if len(args) < 3 {
			return fmt.Errorf("FILE READ: need PATH,VAR")
		}
		name := strings.TrimSpace(args[2])
		g.declare(name, TypeString)
		fmt.Fprintf(&g.code, "    static char %s_buf[65536]; gs_file_read(%s, %s_buf, sizeof(%s_buf)); %s = %s_buf;\n", name, g.cValue(args[1], TypeString), name, name, name, name)
	case "WRITE":
		if len(args) < 3 {
			return fmt.Errorf("FILE WRITE: need PATH,CONTENT")
		}
		fmt.Fprintf(&g.code, "    gs_file_write(%s, %s);\n", g.cValue(args[1], TypeString), g.cStringExpr(strings.Join(args[2:], ",")))
	case "APPEND":
		if len(args) < 3 {
			return fmt.Errorf("FILE APPEND: need PATH,CONTENT")
		}
		fmt.Fprintf(&g.code, "    gs_file_append(%s, %s);\n", g.cValue(args[1], TypeString), g.cStringExpr(strings.Join(args[2:], ",")))
	default:
		return fmt.Errorf("FILE: compiler does not support operation %s", op)
	}
	return nil
}

func (g *Generator) genHash(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return
	}
	algo := strings.ToUpper(parts[0])
	src := strings.TrimPrefix(parts[1], "@")
	var sum string
	switch algo {
	case "MD5":
		h := md5.Sum([]byte(src))
		sum = fmt.Sprintf("%x", h[:])
	case "SHA1":
		h := sha1.Sum([]byte(src))
		sum = fmt.Sprintf("%x", h[:])
	case "SHA256":
		h := sha256.Sum256([]byte(src))
		sum = fmt.Sprintf("%x", h[:])
	case "SHA512":
		h := sha512.Sum512([]byte(src))
		sum = fmt.Sprintf("%x", h[:])
	default:
		sum = ""
	}
	g.declare(name, TypeString)
	fmt.Fprintf(&g.code, "    %s = %s;\n", name, g.cValue(sum, TypeString))
}

func (g *Generator) genBase64(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return
	}
	mode := strings.ToUpper(parts[0])
	val := parts[1]
	out := ""
	if mode == "ENC" {
		out = base64.StdEncoding.EncodeToString([]byte(val))
	} else if mode == "DEC" {
		if dec, err := base64.StdEncoding.DecodeString(val); err == nil {
			out = string(dec)
		}
	}
	g.declare(name, TypeString)
	fmt.Fprintf(&g.code, "    %s = %s;\n", name, g.cValue(out, TypeString))
}

func (g *Generator) genHex(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return
	}
	mode := strings.ToUpper(parts[0])
	val := parts[1]
	out := ""
	if mode == "ENC" {
		out = hex.EncodeToString([]byte(val))
	} else if mode == "DEC" {
		if dec, err := hex.DecodeString(val); err == nil {
			out = string(dec)
		}
	}
	g.declare(name, TypeString)
	fmt.Fprintf(&g.code, "    %s = %s;\n", name, g.cValue(out, TypeString))
}

func (g *Generator) genDir(args []string) error {
	if len(args) < 1 {
		return fmt.Errorf("FDIR: need OP[,PATH]")
	}
	op := strings.ToUpper(strings.TrimSpace(args[0]))
	switch op {
	case "MAKE":
		path := "."
		if len(args) >= 2 {
			path = args[1]
		}
		fmt.Fprintf(&g.code, "    gs_dir_make(%s);\n", g.cValue(path, TypeString))
	case "DEL":
		if len(args) < 2 {
			return fmt.Errorf("FDIR DEL: need PATH")
		}
		fmt.Fprintf(&g.code, "    gs_dir_del(%s);\n", g.cValue(args[1], TypeString))
	case "LIST":
		if len(args) < 3 {
			return fmt.Errorf("FDIR LIST: need PATH,KEY")
		}
		name := strings.TrimSpace(args[2])
		g.declare(name, TypeString)
		fmt.Fprintf(&g.code, "    static char %s_buf[65536]; gs_dir_list(%s, %s_buf, sizeof(%s_buf)); %s = %s_buf;\n", name, g.cValue(args[1]+"\\\\*", TypeString), name, name, name, name)
	default:
		return fmt.Errorf("FDIR: compiler does not support operation %s", op)
	}
	return nil
}

func (g *Generator) genLink(args []string) error {
	if len(args) < 3 {
		return fmt.Errorf("LINK: need TYPE,SRC,DST")
	}
	typ := strings.ToUpper(strings.TrimSpace(args[0]))
	switch typ {
	case "SYM", "HARD", "JUNC":
		fmt.Fprintf(&g.code, "    gs_link(%s, %s, %s);\n", g.cValue(typ, TypeString), g.cValue(args[1], TypeString), g.cValue(args[2], TypeString))
	default:
		return fmt.Errorf("LINK: compiler does not support type %s", typ)
	}
	return nil
}

func (g *Generator) genIfex(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("IFEX: need COND,CMD,ARGS")
	}
	cond := g.cCond(args[0])
	inner := gsStatement(args[1], args[2:])
	fmt.Fprintf(&g.code, "    if (%s) {\n", cond)
	if err := g.genStatements(gsStatementAdapters{inner}.toStatements()); err != nil {
		return err
	}
	g.code.WriteString("    }\n")
	return nil
}

func (g *Generator) genWhen(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("WHEN: need block,condition")
	}
	body, ok := g.prog.Blocks[args[0]]
	if !ok {
		return fmt.Errorf("WHEN: block %s not found", args[0])
	}
	fmt.Fprintf(&g.code, "    if (%s) {\n", g.cCond(args[1]))
	if err := g.genStatements(body); err != nil {
		return err
	}
	g.code.WriteString("    }\n")
	return nil
}

func (g *Generator) genLoop(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("LOOP: need block,count")
	}
	body, ok := g.prog.Blocks[args[0]]
	if !ok {
		return fmt.Errorf("LOOP: block %s not found", args[0])
	}
	g.declare("INDEX", TypeFloat)
	idx := g.nextTemp("loop")
	fmt.Fprintf(&g.code, "    for (int %s = 0; %s < (int)(%s); ++%s) {\n", idx, idx, g.cArg(args[1]), idx)
	fmt.Fprintf(&g.code, "    INDEX = (double)%s;\n", idx)
	if err := g.genStatements(body); err != nil {
		return err
	}
	g.code.WriteString("    }\n")
	return nil
}

func (g *Generator) genForx(args []string) error {
	if len(args) < 3 {
		return fmt.Errorf("FORX: need block,pattern,dir")
	}
	body, ok := g.prog.Blocks[args[0]]
	if !ok {
		return fmt.Errorf("FORX: block %s not found", args[0])
	}
	pattern := strings.Trim(args[1], "\"")
	dir := strings.Trim(args[2], "\"")
	fullPattern := dir
	if fullPattern == "" || fullPattern == "." {
		fullPattern = pattern
	} else {
		fullPattern = strings.TrimRight(fullPattern, `\\/`) + `\\` + pattern
	}
	g.declare("FILE", TypeString)
	idx := g.nextTemp("forx")
	fmt.Fprintf(&g.code, "    { WIN32_FIND_DATAA __fd_%s; HANDLE __h_%s = FindFirstFileA(%s, &__fd_%s);\n", idx, idx, g.cValue(fullPattern, TypeString), idx)
	fmt.Fprintf(&g.code, "    if (__h_%s != INVALID_HANDLE_VALUE) { do {\n", idx)
	fmt.Fprintf(&g.code, "    static char FILE_buf_%s[MAX_PATH]; strcpy(FILE_buf_%s, __fd_%s.cFileName); FILE = FILE_buf_%s;\n", idx, idx, idx, idx)
	if err := g.genStatements(body); err != nil {
		return err
	}
	fmt.Fprintf(&g.code, "    } while (FindNextFileA(__h_%s, &__fd_%s)); FindClose(__h_%s); } }\n", idx, idx, idx)
	return nil
}

func (g *Generator) genJsonRead(args []string) error {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return fmt.Errorf("JSON: need KEY=FILE_OR_TEXT,PATH")
	}
	val, err := g.compileTimeJsonValue(parts[0], parts[1])
	if err != nil {
		return err
	}
	g.declare(name, TypeString)
	fmt.Fprintf(&g.code, "    %s = %s;\n", name, g.cValue(val, TypeString))
	return nil
}

func (g *Generator) genJsonLen(args []string) error {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return fmt.Errorf("JSNL: need KEY=FILE_OR_TEXT,PATH")
	}
	arr, err := g.compileTimeJsonArray(parts[0], parts[1])
	if err != nil {
		return err
	}
	g.declare(name, TypeFloat)
	fmt.Fprintf(&g.code, "    %s = %d;\n", name, len(arr))
	return nil
}

func (g *Generator) genJsonSet(args []string) error {
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
	jsonPath = strings.TrimSpace(jsonPath)
	if !strings.HasPrefix(jsonPath, "$") {
		return fmt.Errorf("JSNS: JSON path must start with $")
	}
	fmt.Fprintf(&g.code, "    gs_json_set(%s, %s, %s);\n", g.cValue(file, TypeString), g.cValue(jsonPath, TypeString), g.cValue(value, TypeString))
	return nil
}

func (g *Generator) genRegistry(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("REGI: need GET/SET/DEL,PATH[,NAME[,VALUE[,TYPE]]]")
	}
	op := strings.ToUpper(strings.TrimSpace(args[0]))
	switch op {
	case "GET":
		if len(args) < 4 {
			return fmt.Errorf("REGI GET: need PATH,NAME,VAR")
		}
		name := strings.TrimSpace(args[3])
		g.declare(name, TypeString)
		fmt.Fprintf(&g.code, "    static char %s_buf[512]; gs_reg_get(%s, %s, %s_buf, sizeof(%s_buf)); %s = %s_buf;\n", name, g.cValue(args[1], TypeString), g.cValue(args[2], TypeString), name, name, name, name)
	case "SET":
		if len(args) < 5 {
			return fmt.Errorf("REGI SET: need PATH,NAME,VALUE,TYPE")
		}
		fmt.Fprintf(&g.code, "    gs_reg_set(%s, %s, %s, %s);\n", g.cValue(args[1], TypeString), g.cValue(args[2], TypeString), g.cValue(args[3], TypeString), g.cValue(args[4], TypeString))
	case "DEL", "DELETE":
		name := ""
		if len(args) >= 3 {
			name = args[2]
		}
		fmt.Fprintf(&g.code, "    gs_reg_del(%s, %s);\n", g.cValue(args[1], TypeString), g.cValue(name, TypeString))
	default:
		return fmt.Errorf("REGI: compiler does not support operation %s", op)
	}
	return nil
}

func (g *Generator) genService(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("SERV: need START/STOP/RESTART/STATUS,NAME")
	}
	op := strings.ToUpper(strings.TrimSpace(args[0]))
	switch op {
	case "START", "STOP", "RESTART", "STATUS":
		fmt.Fprintf(&g.code, "    gs_service(%s, %s);\n", g.cValue(op, TypeString), g.cValue(args[1], TypeString))
	default:
		return fmt.Errorf("SERV: compiler does not support operation %s", op)
	}
	return nil
}

func (g *Generator) genTask(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("TASK: need RUN/DEL/QUERY/CREATE,NAME[,...]")
	}
	op := strings.ToUpper(strings.TrimSpace(args[0]))
	trigger := ""
	command := ""
	switch op {
	case "RUN", "DEL", "DELETE", "QUERY", "STATUS":
	case "CREATE":
		if len(args) < 4 {
			return fmt.Errorf("TASK CREATE: need NAME,TRIGGER,COMMAND")
		}
		trigger = args[2]
		command = strings.Join(args[3:], ",")
	default:
		return fmt.Errorf("TASK: compiler does not support operation %s", op)
	}
	fmt.Fprintf(&g.code, "    gs_task(%s, %s, %s, %s);\n", g.cValue(op, TypeString), g.cValue(args[1], TypeString), g.cValue(trigger, TypeString), g.cValue(command, TypeString))
	return nil
}

func (g *Generator) genFirewall(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("FWAL: need ADD/DEL,rule args")
	}
	op := strings.ToUpper(strings.TrimSpace(args[0]))
	switch op {
	case "ADD", "DEL", "DELETE":
		fmt.Fprintf(&g.code, "    gs_firewall(%s, %s);\n", g.cValue(op, TypeString), g.cValue(strings.Join(args[1:], ","), TypeString))
	default:
		return fmt.Errorf("FWAL: compiler does not support operation %s", op)
	}
	return nil
}

func (g *Generator) genHTTP(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("HTTP: need METHOD,URL[,BODY]")
	}
	g.useNetRuntime()
	method := strings.ToUpper(strings.TrimSpace(args[0]))
	body := ""
	if len(args) > 2 {
		body = strings.Join(args[2:], ",")
	}
	codeVar := g.nextTemp("http_code")
	fmt.Fprintf(&g.code, "    int %s = 0; memset(HTTP_BODY_buf, 0, sizeof(HTTP_BODY_buf)); gs_http(%s, %s, %s, HTTP_BODY_buf, sizeof(HTTP_BODY_buf), &%s); HTTP_BODY = HTTP_BODY_buf; HTTP_CODE = (double)%s;\n", codeVar, g.cValue(method, TypeString), g.cValue(args[1], TypeString), g.cValue(body, TypeString), codeVar, codeVar)
	g.vars["HTTP_BODY"] = varInfo{typ: TypeString}
	g.vars["HTTP_CODE"] = varInfo{typ: TypeFloat}
	return nil
}

func (g *Generator) genDown(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("DOWN: need URL,FILE")
	}
	g.useNetRuntime()
	fmt.Fprintf(&g.code, "    gs_down(%s, %s);\n", g.cValue(args[0], TypeString), g.cValue(args[1], TypeString))
	return nil
}

func (g *Generator) genUpload(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("UPLD: need FILE,URL")
	}
	g.useNetRuntime()
	codeVar := g.nextTemp("http_code")
	fmt.Fprintf(&g.code, "    int %s = 0; memset(HTTP_BODY_buf, 0, sizeof(HTTP_BODY_buf)); gs_upld(%s, %s, HTTP_BODY_buf, sizeof(HTTP_BODY_buf), &%s); HTTP_BODY = HTTP_BODY_buf; HTTP_CODE = (double)%s;\n", codeVar, g.cValue(args[0], TypeString), g.cValue(args[1], TypeString), codeVar, codeVar)
	g.vars["HTTP_BODY"] = varInfo{typ: TypeString}
	g.vars["HTTP_CODE"] = varInfo{typ: TypeFloat}
	return nil
}

func (g *Generator) compileTimeJsonRoot(src string) (interface{}, error) {
	src = strings.TrimSpace(src)
	var data []byte
	if strings.HasPrefix(src, "@") {
		data = []byte(strings.TrimPrefix(src, "@"))
	} else {
		path := strings.Trim(src, "\"")
		if !filepath.IsAbs(path) && g.srcDir != "" {
			path = filepath.Join(g.srcDir, path)
		}
		b, err := os.ReadFile(path)
		if err != nil {
			return nil, fmt.Errorf("JSON: read %s: %w", src, err)
		}
		data = b
	}
	var root interface{}
	if err := json.Unmarshal(data, &root); err != nil {
		return nil, fmt.Errorf("JSON: parse: %w", err)
	}
	return root, nil
}

func (g *Generator) compileTimeJsonValue(src string, path string) (string, error) {
	root, err := g.compileTimeJsonRoot(src)
	if err != nil {
		return "", err
	}
	cur, err := jsonPathValue(root, path)
	if err != nil {
		return "", err
	}
	switch v := cur.(type) {
	case string:
		return v, nil
	case float64:
		return strconv.FormatFloat(v, 'f', -1, 64), nil
	case bool:
		if v {
			return "true", nil
		}
		return "false", nil
	case nil:
		return "", nil
	default:
		b, _ := json.Marshal(v)
		return string(b), nil
	}
}

func (g *Generator) compileTimeJsonArray(src string, path string) ([]interface{}, error) {
	root, err := g.compileTimeJsonRoot(src)
	if err != nil {
		return nil, err
	}
	cur, err := jsonPathValue(root, path)
	if err != nil {
		return nil, err
	}
	arr, ok := cur.([]interface{})
	if !ok {
		return nil, fmt.Errorf("JSON path does not resolve to array: %s", path)
	}
	return arr, nil
}

func jsonPathValue(root interface{}, path string) (interface{}, error) {
	if !strings.HasPrefix(path, "$") {
		return nil, fmt.Errorf("JSON path must start with $")
	}
	cur := root
	parts := strings.Split(strings.TrimPrefix(path[1:], "."), ".")
	for _, part := range parts {
		if part == "" {
			continue
		}
		key := part
		idx := -1
		if b := strings.Index(part, "["); b >= 0 {
			key = part[:b]
			if e := strings.Index(part[b:], "]"); e > 0 {
				idx, _ = strconv.Atoi(part[b+1 : b+e])
			}
		}
		if key != "" {
			m, ok := cur.(map[string]interface{})
			if !ok {
				return nil, fmt.Errorf("JSON path key %s not in object", key)
			}
			cur = m[key]
		}
		if idx >= 0 {
			arr, ok := cur.([]interface{})
			if !ok || idx >= len(arr) {
				return nil, fmt.Errorf("JSON path index %d invalid", idx)
			}
			cur = arr[idx]
		}
	}
	return cur, nil
}

func (g *Generator) cCond(expr string) string {
	expr = strings.TrimSpace(expr)
	for name, info := range g.vars {
		repl := "(" + name + ")"
		if info.typ == TypeString {
			repl = name
		}
		expr = strings.ReplaceAll(expr, "%"+name+"%", repl)
		expr = strings.ReplaceAll(expr, "%@"+name+"%", repl)
	}
	expr = strings.ReplaceAll(expr, " AND ", " && ")
	expr = strings.ReplaceAll(expr, " OR ", " || ")
	expr = strings.ReplaceAll(expr, " NOT ", " !")
	return expr
}

func (g *Generator) nextTemp(prefix string) string {
	g.nextID++
	return fmt.Sprintf("__gs_%s_%d", prefix, g.nextID)
}

type gsStatementAdapter struct {
	cmd  string
	args []string
}

func gsStatement(cmd string, args []string) gsStatementAdapter {
	return gsStatementAdapter{cmd: cmd, args: args}
}

func (a gsStatementAdapter) toStatement() gs.Statement {
	return gs.Statement{Cmd: strings.ToUpper(a.cmd), Args: a.args}
}

type gsStatementAdapters []gsStatementAdapter

func (as gsStatementAdapters) toStatements() []gs.Statement {
	out := make([]gs.Statement, len(as))
	for i, a := range as {
		out[i] = a.toStatement()
	}
	return out
}

func (g *Generator) genSubstr(args []string, fn string, extraArgs int) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 1+extraArgs {
		return
	}
	g.declare(name, TypeString)
	fmt.Fprintf(&g.code, "    static char %s_buf[65536]; %s(%s, %s", name, fn, g.cValue(parts[0], TypeString), g.cArg(parts[1]))
	if extraArgs >= 2 {
		fmt.Fprintf(&g.code, ", %s", g.cArg(parts[2]))
	}
	fmt.Fprintf(&g.code, ", %s_buf, sizeof(%s_buf)); %s = %s_buf;\n", name, name, name, name)
}

func (g *Generator) genRegex(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return
	}
	g.declare(name, TypeString)
	group := "0"
	if len(parts) >= 3 {
		group = parts[2]
	}
	fmt.Fprintf(&g.code, "    %s = gs_regex(%s, %s, %s);\n", name, g.cValue(parts[0], TypeString), g.cValue(parts[1], TypeString), g.cArg(group))
}

func (g *Generator) genRegexSub(args []string) {
	if len(args) < 3 {
		return
	}
	k := args[0]
	s := args[1]
	pat := args[2]
	repl := ""
	if len(args) >= 4 {
		repl = args[3]
	}
	g.declare(k, TypeString)
	fmt.Fprintf(&g.code, "    %s = gs_regex_sub(%s, %s, %s);\n", k, g.cValue(s, TypeString), g.cValue(pat, TypeString), g.cValue(repl, TypeString))
}

func (g *Generator) genXmlRead(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 2 {
		return
	}
	g.declare(name, TypeString)
	fmt.Fprintf(&g.code, "    static char %s_buf[65536]; gs_xml_read2(%s, %s, %s_buf, sizeof(%s_buf)); %s = %s_buf;\n", name, g.cValue(parts[0], TypeString), g.cValue(parts[1], TypeString), name, name, name, name)
}

func (g *Generator) genXmlWrite(args []string) {
	if len(args) < 2 {
		return
	}
	file := args[0]
	idx := strings.Index(args[1], "=")
	if idx < 0 {
		return
	}
	xpath := args[1][:idx]
	value := args[1][idx+1:]
	if len(args) > 2 {
		value = value + "," + strings.Join(args[2:], ",")
	}
	fmt.Fprintf(&g.code, "    gs_xml_write2(%s, %s, %s);\n", g.cValue(file, TypeString), g.cValue(xpath, TypeString), g.cValue(value, TypeString))
}

func (g *Generator) genAes(args []string) {
	name, rest := splitKV(args)
	parts := splitCSV(rest)
	if name == "" || len(parts) < 4 {
		return
	}
	mode := parts[0]
	key := parts[1]
	iv := parts[2]
	text := parts[3]
	g.declare(name, TypeString)
	fmt.Fprintf(&g.code, "    static char %s_buf[65536]; gs_aes(%s, %s, %s, %s, %s_buf, sizeof(%s_buf)); %s = %s_buf;\n", name, g.cValue(mode, TypeString), g.cValue(key, TypeString), g.cValue(iv, TypeString), g.cValue(text, TypeString), name, name, name, name)
}
