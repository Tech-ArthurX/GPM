package gs

import "strings"

// TokenType classifies a lexical token.
type TokenType int

const (
	TokCommand TokenType = iota // 4-letter command name
	TokIdent                    // identifier or bare string arg
	TokString                   // double-quoted string
	TokComma                    // argument separator ','
	TokEqual                    // '=' for assignment
	TokNewline                  // line break
	TokEOF                      // end of input
)

// Token is a single lexical element.
type Token struct {
	Type  TokenType
	Value string
	Line  int
}

// Lexer tokenises GS source text.
type Lexer struct {
	src  string
	pos  int
	line int
}

// NewLexer creates a lexer for the given source text.
func NewLexer(src string) *Lexer {
	return &Lexer{src: src, pos: 0, line: 1}
}

func (l *Lexer) peek() byte {
	if l.pos >= len(l.src) {
		return 0
	}
	return l.src[l.pos]
}

func (l *Lexer) advance() byte {
	ch := l.src[l.pos]
	l.pos++
	if ch == '\n' {
		l.line++
	}
	return ch
}

func (l *Lexer) skipWhitespace() {
	for l.pos < len(l.src) {
		ch := l.peek()
		if ch == ' ' || ch == '\t' || ch == '\r' {
			l.advance()
			continue
		}
		break
	}
}

func (l *Lexer) skipComment() {
	for l.pos < len(l.src) && l.peek() != '\n' {
		l.advance()
	}
}

func isCommandName(s string) bool {
	if len(s) != 4 {
		return false
	}
	for _, ch := range s {
		if ch < 'A' || ch > 'Z' {
			return false
		}
	}
	return true
}

// NextToken returns the next token, or TokEOF.
func (l *Lexer) NextToken() Token {
	l.skipWhitespace()
	if l.pos >= len(l.src) {
		return Token{Type: TokEOF, Line: l.line}
	}

	ch := l.peek()

	// Newline
	if ch == '\n' {
		l.advance()
		return Token{Type: TokNewline, Value: "\n", Line: l.line}
	}

	// Comma
	if ch == ',' {
		l.advance()
		return Token{Type: TokComma, Value: ",", Line: l.line}
	}

	// Equal
	if ch == '=' {
		l.advance()
		return Token{Type: TokEqual, Value: "=", Line: l.line}
	}

	// Double-quoted string
	if ch == '"' {
		return l.readString()
	}

	// Comment: line starts with ; # // (after whitespace)
	// Also inline // comment
	if ch == ';' || ch == '#' {
		l.skipComment()
		return l.NextToken()
	}
	if ch == '/' && l.pos+1 < len(l.src) && l.src[l.pos+1] == '/' {
		l.skipComment()
		return l.NextToken()
	}

	// Bare word / command name / identifier
	return l.readIdent()
}

func (l *Lexer) readString() Token {
	line := l.line
	l.advance() // skip opening "
	var sb strings.Builder
	for l.pos < len(l.src) {
		ch := l.advance()
		if ch == '"' {
			break
		}
		if ch == '\\' {
			if l.pos < len(l.src) {
				esc := l.advance()
				switch esc {
				case 'n':
					sb.WriteByte('\n')
				case 't':
					sb.WriteByte('\t')
				case '"':
					sb.WriteByte('"')
				case '\\':
					sb.WriteByte('\\')
				default:
					sb.WriteByte('\\')
					sb.WriteByte(esc)
				}
			}
			continue
		}
		sb.WriteByte(ch)
	}
	return Token{Type: TokString, Value: sb.String(), Line: line}
}

func (l *Lexer) readIdent() Token {
	line := l.line
	var sb strings.Builder
	for l.pos < len(l.src) {
		ch := l.peek()
		if ch == ',' || ch == '=' || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '"' {
			break
		}
		sb.WriteByte(byte(l.advance()))
	}
	val := sb.String()
	tt := TokIdent
	if isCommandName(val) {
		tt = TokCommand
	}
	return Token{Type: tt, Value: val, Line: line}
}

// Tokenize returns all tokens for the source.
func Tokenize(src string) []Token {
	var tokens []Token
	l := NewLexer(src)
	for {
		t := l.NextToken()
		tokens = append(tokens, t)
		if t.Type == TokEOF {
			break
		}
	}
	return tokens
}
