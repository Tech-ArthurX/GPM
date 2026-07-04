package gs

import (
	"fmt"
	"strings"
)

// Statement is a single gs command line.
type Statement struct {
	Cmd  string   // command name, or internal block marker
	Args []string // raw argument strings, in order
	Line int      // source line for error messages
}

// Program is a parsed gs script.
type Program struct {
	// Main is the top-level statement list (the implicit "MAIN" body).
	Main []Statement
	// Subs maps subroutine name to its body.
	Subs map[string][]Statement
	// Blocks maps synthetic sub names for WHEN/LOOP/FORX bodies.
	// The opening statement stays in Main/Subs; the body is stored here
	// under a generated key like "__GS_WHEN_0__".
	Blocks map[string][]Statement
}

// Parser turns tokens into a Program. Each line is one statement.
type Parser struct {
	tokens []Token
	pos    int
}

// NewParser creates a parser for the given tokens.
func NewParser(tokens []Token) *Parser {
	return &Parser{tokens: tokens, pos: 0}
}

func (p *Parser) peek() Token {
	if p.pos >= len(p.tokens) {
		return Token{Type: TokEOF}
	}
	return p.tokens[p.pos]
}

func (p *Parser) advance() Token {
	t := p.peek()
	if p.pos < len(p.tokens) {
		p.pos++
	}
	return t
}

func (p *Parser) skipNewlines() {
	for p.peek().Type == TokNewline {
		p.advance()
	}
}

// parseLine reads one logical statement up to the next newline / EOF.
// Returns (stmt, ok). ok=false means EOF or empty line.
func (p *Parser) parseLine() (Statement, bool) {
	p.skipNewlines()
	tok := p.peek()
	if tok.Type == TokEOF {
		return Statement{}, false
	}

	stmt := Statement{Line: tok.Line}

	// First token: command name. Accept internal _END as special.
	first := p.advance()
	switch {
	case first.Type == TokCommand:
		stmt.Cmd = first.Value
	case first.Type == TokIdent && first.Value == "_END":
		stmt.Cmd = first.Value
	case first.Type == TokIdent:
		// Allow lowercase / unusual commands too; uppercase to be lenient
		stmt.Cmd = strings.ToUpper(first.Value)
	default:
		// Skip stray tokens until newline.
		for p.peek().Type != TokNewline && p.peek().Type != TokEOF {
			p.advance()
		}
		return Statement{}, false
	}

	// Read args: collect tokens up to newline / EOF.
	// Args are comma-separated. Each arg may contain spaces if quoted
	// or as a single bare word; multiple bare words separated by space
	// before the first comma are joined with a single space (so
	// `EXEC notepad foo bar` becomes args ["notepad foo bar"]).
	var current strings.Builder
	flush := func() {
		s := strings.TrimSpace(current.String())
		stmt.Args = append(stmt.Args, s)
		current.Reset()
	}

	hasContent := false
	for {
		t := p.peek()
		if t.Type == TokEOF || t.Type == TokNewline {
			break
		}
		p.advance()
		switch t.Type {
		case TokComma:
			flush()
			hasContent = false
		case TokEqual:
			// Treat '=' as part of the argument text (e.g. SETV K=V).
			if hasContent {
				current.WriteByte('=')
			} else {
				current.WriteByte('=')
				hasContent = true
			}
		case TokString:
			if hasContent && current.Len() > 0 {
				current.WriteByte(' ')
			}
			current.WriteString(t.Value)
			hasContent = true
		case TokIdent, TokCommand:
			if hasContent && current.Len() > 0 {
				current.WriteByte(' ')
			}
			current.WriteString(t.Value)
			hasContent = true
		}
	}
	if hasContent || current.Len() > 0 {
		flush()
	}

	return stmt, true
}

// Parse consumes all tokens and returns a Program.
func (p *Parser) Parse() (*Program, error) {
	prog := &Program{
		Subs:   make(map[string][]Statement),
		Blocks: make(map[string][]Statement),
	}
	type ctxFrame struct {
		name string
		kind string // "sub" or "block"
		body []Statement
	}
	var stack []ctxFrame
	blockID := 0

	nextBlockID := func() int {
		blockID++
		return blockID
	}
	appendStmt := func(stmt Statement) {
		if len(stack) == 0 {
			prog.Main = append(prog.Main, stmt)
			return
		}
		stack[len(stack)-1].body = append(stack[len(stack)-1].body, stmt)
	}
	pushSub := func(name string) {
		stack = append(stack, ctxFrame{name: strings.ToUpper(name), kind: "sub"})
	}
	pushBlock := func(name string) {
		stack = append(stack, ctxFrame{name: name, kind: "block"})
	}
	popFrame := func(line int) error {
		if len(stack) == 0 {
			return fmt.Errorf("line %d: _END without FUNC/block", line)
		}
		frame := stack[len(stack)-1]
		stack = stack[:len(stack)-1]
		if frame.kind == "block" {
			prog.Blocks[frame.name] = frame.body
		} else {
			prog.Subs[strings.ToUpper(frame.name)] = frame.body
		}
		return nil
	}

	for {
		stmt, ok := p.parseLine()
		if !ok {
			if p.peek().Type == TokEOF {
				break
			}
			continue
		}

		switch stmt.Cmd {
		case "_SUB":
			return nil, fmt.Errorf("line %d: unsupported _SUB syntax; use FUNC name with an indented body", stmt.Line)
		case "FUNK":
			return nil, fmt.Errorf("line %d: unsupported FUNK syntax; use FUNC", stmt.Line)
		case "FUNC":
			if len(stmt.Args) == 0 || stmt.Args[0] == "" {
				return nil, fmt.Errorf("line %d: FUNC needs a name", stmt.Line)
			}
			pushSub(normalizePhaseName(stmt.Args[0]))
		case "_PREI", "_PREINST", "_PRE", "_PREINSTALL", "_INST", "_INSTALL", "_INSTALLING", "_POST", "_POSTINST", "_POSTINSTALL", "_PREU", "_PREUNINST", "_PREUNINSTALL", "_UNIN", "_UNINST", "_UNINSTALL", "_UNINSTALLING", "_PSTU", "_POSTU", "_POSTUNINST", "_POSTUNINSTALL":
			pushSub(normalizePhaseName(strings.TrimPrefix(stmt.Cmd, "_")))
		case "WHEN", "LOOP", "FORX":
			id := nextBlockID()
			synthName := fmt.Sprintf("__GS_%s_%d__", stmt.Cmd, id)
			stmt.Args = append([]string{synthName}, stmt.Args...)
			appendStmt(stmt)
			pushBlock(synthName)
		case "_END":
			if err := popFrame(stmt.Line); err != nil {
				return nil, err
			}
		default:
			appendStmt(stmt)
		}
	}

	if len(stack) > 0 {
		return nil, fmt.Errorf("unterminated block %q", stack[len(stack)-1].name)
	}
	return prog, nil
}

var blockMarkers = []struct{ start, end, cmd string }{
	{"CGSB", "CGSE", "CGSB"},
	{"PGCB", "PGCE", "PGCB"},
	{"AGCB", "AGCE", "AGCB"},
}

func sourceIndent(s string) int {
	n := 0
	for _, r := range s {
		switch r {
		case ' ':
			n++
		case '\t':
			n += 4
		default:
			return n
		}
	}
	return n
}

func preprocessFuncSyntax(src string) string {
	type frame struct{ indent int }
	var out []string
	var stack []frame
	lines := strings.Split(src, "\n")
	closeTop := func() {
		out = append(out, "_END")
		stack = stack[:len(stack)-1]
	}
	for _, line := range lines {
		trim := strings.TrimSpace(strings.TrimPrefix(line, string([]byte{0xEF, 0xBB, 0xBF})))
		indent := sourceIndent(line)
		lower := strings.ToLower(trim)

		if trim == "" || strings.HasPrefix(trim, ";") || strings.HasPrefix(trim, "#") || strings.HasPrefix(trim, "//") {
			out = append(out, line)
			continue
		}

		for len(stack) > 0 && indent <= stack[len(stack)-1].indent {
			closeTop()
		}

		if strings.HasPrefix(lower, "func ") {
			name := strings.TrimSpace(trim[4:])
			name = strings.TrimSuffix(name, ":")
			out = append(out, "FUNC "+name)
			stack = append(stack, frame{indent: indent})
			continue
		}

		out = append(out, line)
	}
	for len(stack) > 0 {
		closeTop()
	}
	return strings.Join(out, "\n")
}

func preprocessIfSyntax(src string) string {
	type frame struct {
		indent int
		flag   string
	}
	var out []string
	var stack []frame
	counter := 0
	lines := strings.Split(src, "\n")

	indentOf := sourceIndent
	closeTop := func() {
		out = append(out, "_END")
		stack = stack[:len(stack)-1]
	}
	isElifElseForTop := func(trim string, indent int) bool {
		if len(stack) == 0 || stack[len(stack)-1].indent != indent {
			return false
		}
		lower := strings.ToLower(trim)
		return strings.HasPrefix(lower, "elif ") || lower == "elif" || strings.HasPrefix(lower, "else")
	}

	for _, line := range lines {
		trim := strings.TrimSpace(strings.TrimPrefix(line, string([]byte{0xEF, 0xBB, 0xBF})))
		indent := indentOf(line)
		lower := strings.ToLower(trim)

		if trim == "" || strings.HasPrefix(trim, ";") || strings.HasPrefix(trim, "#") || strings.HasPrefix(trim, "//") {
			out = append(out, line)
			continue
		}

		for len(stack) > 0 && indent <= stack[len(stack)-1].indent && !isElifElseForTop(trim, indent) {
			closeTop()
		}

		if strings.HasPrefix(lower, "if ") {
			cond := strings.TrimSpace(trim[2:])
			cond = strings.TrimSuffix(cond, ":")
			counter++
			flag := fmt.Sprintf("__IF_%d", counter)
			out = append(out, fmt.Sprintf("SETV %s=0", flag))
			out = append(out, "WHEN "+quoteGSString(cond))
			out = append(out, fmt.Sprintf("SETV %s=1", flag))
			stack = append(stack, frame{indent: indent, flag: flag})
			continue
		}

		if strings.HasPrefix(lower, "elif ") {
			if len(stack) == 0 || stack[len(stack)-1].indent != indent {
				out = append(out, line)
				continue
			}
			flag := stack[len(stack)-1].flag
			out = append(out, "_END")
			cond := strings.TrimSpace(trim[4:])
			cond = strings.TrimSuffix(cond, ":")
			out = append(out, "WHEN "+quoteGSString(fmt.Sprintf("%%@%s%% == 0 AND (%s)", flag, cond)))
			out = append(out, fmt.Sprintf("SETV %s=1", flag))
			continue
		}

		if lower == "else" || lower == "else:" {
			if len(stack) == 0 || stack[len(stack)-1].indent != indent {
				out = append(out, line)
				continue
			}
			flag := stack[len(stack)-1].flag
			out = append(out, "_END")
			out = append(out, "WHEN "+quoteGSString(fmt.Sprintf("%%@%s%% == 0", flag)))
			out = append(out, fmt.Sprintf("SETV %s=1", flag))
			continue
		}

		out = append(out, line)
	}
	for len(stack) > 0 {
		closeTop()
	}
	return strings.Join(out, "\n")
}

func quoteGSString(s string) string {
	s = strings.ReplaceAll(s, "\\", "\\\\")
	s = strings.ReplaceAll(s, "\"", "\\\"")
	return "\"" + s + "\""
}

func preprocessBlocks(src string) string {
	var out []string
	lines := strings.Split(src, "\n")
	for i := 0; i < len(lines); i++ {
		line := lines[i]
		trim := strings.TrimSpace(strings.TrimPrefix(line, string([]byte{0xEF, 0xBB, 0xBF})))
		matched := false
		for _, m := range blockMarkers {
			if trim == m.start {
				var block []string
				i++
				for ; i < len(lines); i++ {
					endTrim := strings.TrimSpace(lines[i])
					if endTrim == m.end {
						break
					}
					block = append(block, lines[i])
				}
				raw := strings.Join(block, "\n")
				raw = strings.ReplaceAll(raw, "\\", "\\\\")
				raw = strings.ReplaceAll(raw, "\"", "\\\"")
				raw = strings.ReplaceAll(raw, "\n", "\\n")
				out = append(out, m.cmd+" \""+raw+"\"")
				matched = true
				break
			}
		}
		if !matched {
			out = append(out, line)
		}
	}
	return strings.Join(out, "\n")
}

// ParseString is a convenience wrapper.
func ParseString(src string) (*Program, error) {
	src = preprocessFuncSyntax(src)
	src = preprocessIfSyntax(src)
	src = preprocessBlocks(src)
	return NewParser(Tokenize(src)).Parse()
}
