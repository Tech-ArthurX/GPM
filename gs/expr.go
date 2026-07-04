package gs

import (
	"fmt"
	"strconv"
	"strings"
)

// evalBool parses and evaluates a simple boolean expression.
// Supported operators (in precedence order from low to high):
//
//	OR   -> logical or
//	AND  -> logical and
//	NOT  -> unary negation
//	== != > < >= <=  -> comparison
//	( )  -> grouping
//
// Operands are strings; numeric comparison when both sides parse as numbers.
func evalBool(expr string, lookup func(string) (string, bool)) (bool, error) {
	expr = strings.TrimSpace(expr)
	if expr == "" {
		return false, fmt.Errorf("empty expression")
	}

	tokens := tokenizeExpr(expr)
	if len(tokens) == 0 {
		return false, fmt.Errorf("empty expression")
	}

	result, _, err := evalOr(tokens, 0, lookup)
	if err != nil {
		return false, err
	}
	return result, nil
}

// exprToken is one element of the infix expression.
type exprToken struct {
	kind byte   // 'v'=value, 'o'=op, '('=lparen, ')'=rparen
	val  string // the literal string or operator text
}

func tokIsOp(s string) bool {
	switch s {
	case "==", "!=", ">", "<", ">=", "<=", "AND", "OR", "NOT":
		return true
	}
	return false
}

func tokPrec(s string) int {
	switch s {
	case "OR":
		return 1
	case "AND":
		return 2
	case "NOT":
		return 3
	case "==", "!=", ">", "<", ">=", "<=":
		return 4
	}
	return 0
}

// tokenizeExpr tokenises a condition expression into flat tokens.
func tokenizeExpr(expr string) []exprToken {
	var out []exprToken
	i := 0
	n := len(expr)
	for i < n {
		ch := expr[i]
		if ch == ' ' || ch == '\t' {
			i++
			continue
		}
		if ch == '(' {
			out = append(out, exprToken{kind: '(', val: "("})
			i++
			continue
		}
		if ch == ')' {
			out = append(out, exprToken{kind: ')', val: ")"})
			i++
			continue
		}
		// Multi-char operators: == != >= <=
		if i+1 < n {
			two := expr[i : i+2]
			switch two {
			case "==", "!=", ">=", "<=":
				out = append(out, exprToken{kind: 'o', val: two})
				i += 2
				continue
			}
		}
		// Single-char operators: > <
		if ch == '>' || ch == '<' {
			out = append(out, exprToken{kind: 'o', val: string(ch)})
			i++
			continue
		}
		// Word operators: AND, OR, NOT
		if isAlpha(ch) {
			j := i
			for j < n && isAlphaNum(expr[j]) {
				j++
			}
			word := strings.ToUpper(expr[i:j])
			if tokIsOp(word) {
				out = append(out, exprToken{kind: 'o', val: word})
			} else {
				out = append(out, exprToken{kind: 'v', val: expr[i:j]})
			}
			i = j
			continue
		}
		// Number or quoted string as value
		if ch == '"' {
			j := i + 1
			for j < n && expr[j] != '"' {
				if expr[j] == '\\' {
					j++
				}
				j++
			}
			if j < n {
				j++ // skip closing "
			}
			out = append(out, exprToken{kind: 'v', val: expr[i:j]})
			i = j
			continue
		}
		// Non-space, non-operator -> presumably a value (number, ident, etc)
		j := i
		for j < n && expr[j] != ' ' && expr[j] != '\t' && expr[j] != ')' && expr[j] != '=' && expr[j] != '!' && expr[j] != '>' && expr[j] != '<' {
			j++
		}
		out = append(out, exprToken{kind: 'v', val: expr[i:j]})
		i = j
	}
	return out
}

func isAlpha(ch byte) bool {
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'
}
func isAlphaNum(ch byte) bool {
	return isAlpha(ch) || (ch >= '0' && ch <= '9')
}

// --- recursive descent expression parser ---

func evalOr(tokens []exprToken, pos int, lookup func(string) (string, bool)) (bool, int, error) {
	left, pos, err := evalAnd(tokens, pos, lookup)
	if err != nil {
		return false, pos, err
	}
	for pos < len(tokens) && tokens[pos].kind == 'o' && tokens[pos].val == "OR" {
		pos++
		right, newPos, err := evalAnd(tokens, pos, lookup)
		if err != nil {
			return false, newPos, err
		}
		left = left || right
		pos = newPos
	}
	return left, pos, nil
}

func evalAnd(tokens []exprToken, pos int, lookup func(string) (string, bool)) (bool, int, error) {
	left, pos, err := evalNot(tokens, pos, lookup)
	if err != nil {
		return false, pos, err
	}
	for pos < len(tokens) && tokens[pos].kind == 'o' && tokens[pos].val == "AND" {
		pos++
		right, newPos, err := evalNot(tokens, pos, lookup)
		if err != nil {
			return false, newPos, err
		}
		left = left && right
		pos = newPos
	}
	return left, pos, nil
}

func evalNot(tokens []exprToken, pos int, lookup func(string) (string, bool)) (bool, int, error) {
	if pos < len(tokens) && tokens[pos].kind == 'o' && tokens[pos].val == "NOT" {
		pos++
		inner, newPos, err := evalCompare(tokens, pos, lookup)
		if err != nil {
			return false, newPos, err
		}
		return !inner, newPos, nil
	}
	return evalCompare(tokens, pos, lookup)
}

func evalCompare(tokens []exprToken, pos int, lookup func(string) (string, bool)) (bool, int, error) {
	if pos >= len(tokens) {
		return false, pos, fmt.Errorf("unexpected end of expression")
	}

	// Unary NOT handled above; here we expect a value expression.
	// It might be a parenthesised group or a literal.
	if tokens[pos].kind == '(' {
		pos++ // skip (
		inner, newPos, err := evalOr(tokens, pos, lookup)
		if err != nil {
			return false, newPos, err
		}
		if newPos >= len(tokens) || tokens[newPos].kind != ')' {
			return false, newPos, fmt.Errorf("missing )")
		}
		pos = newPos + 1 // skip )

		// After a parenthesised group there might still be a comparison operator
		if pos < len(tokens) && tokens[pos].kind == 'o' && tokens[pos].val != "AND" && tokens[pos].val != "OR" && tokens[pos].val != "NOT" {
			op := tokens[pos].val
			pos++
			right, newPos, err := evalCompare(tokens, pos, lookup)
			if err != nil {
				return false, newPos, err
			}
			result, err := applyCompare(toString(inner, lookup), toString(right, lookup), op)
			if err != nil {
				return false, newPos, err
			}
			return result, newPos, nil
		}

		return inner, pos, nil
	}

	// Expect a value
	if tokens[pos].kind != 'v' {
		return false, pos, fmt.Errorf("expected value, got %q at position %d", tokens[pos].val, pos)
	}

	leftStr := resolveValue(tokens[pos].val, lookup)
	pos++

	// If next is a comparison operator
	if pos < len(tokens) && tokens[pos].kind == 'o' && tokens[pos].val != "AND" && tokens[pos].val != "OR" && tokens[pos].val != "NOT" {
		op := tokens[pos].val
		pos++
		rightStr, newPos, err := parseCompareOperand(tokens, pos, lookup)
		if err != nil {
			return false, newPos, err
		}
		result, err := applyCompare(leftStr, rightStr, op)
		if err != nil {
			return false, newPos, err
		}
		return result, newPos, nil
	}

	// Bare value: is it truthy?
	return isTruthy(leftStr), pos, nil
}

// parseCompareOperand reads the right side of a comparison as a raw value.
func parseCompareOperand(tokens []exprToken, pos int, lookup func(string) (string, bool)) (string, int, error) {
	if pos >= len(tokens) {
		return "", pos, fmt.Errorf("missing comparison operand")
	}
	if tokens[pos].kind == '(' {
		result, newPos, err := evalCompare(tokens, pos, lookup)
		if err != nil {
			return "", newPos, err
		}
		return toString(result, lookup), newPos, nil
	}
	if tokens[pos].kind != 'v' {
		return "", pos, fmt.Errorf("expected comparison operand, got %q", tokens[pos].val)
	}
	return resolveValue(tokens[pos].val, lookup), pos + 1, nil
}

// resolveValue checks if s is a variable reference (starts with %), resolves it,
// or strips quotes from a quoted string, otherwise returns as-is.
func resolveValue(s string, lookup func(string) (string, bool)) string {
	if len(s) >= 3 && s[0] == '%' && s[len(s)-1] == '%' {
		name := strings.TrimPrefix(s[1:len(s)-1], "@")
		if v, ok := lookup(name); ok {
			return v
		}
		return s
	}
	if len(s) >= 2 && s[0] == '"' && s[len(s)-1] == '"' {
		inner := s[1 : len(s)-1]
		inner = strings.ReplaceAll(inner, "\\\"", "\"")
		inner = strings.ReplaceAll(inner, "\\n", "\n")
		inner = strings.ReplaceAll(inner, "\\t", "\t")
		return inner
	}
	return s
}

// toString converts a boolean result back to string for comparison chaining.
func toString(b bool, _ func(string) (string, bool)) string {
	if b {
		return "1"
	}
	return "0"
}

func isTruthy(s string) bool {
	s = strings.TrimSpace(s)
	if s == "" || s == "0" || s == "false" || s == "FALSE" || s == "no" || s == "NO" {
		return false
	}
	return true
}

func applyCompare(left, right, op string) (bool, error) {
	// Try numeric comparison first
	ln, errL := strconv.ParseFloat(strings.TrimSpace(left), 64)
	rn, errR := strconv.ParseFloat(strings.TrimSpace(right), 64)
	canNumeric := errL == nil && errR == nil

	switch op {
	case "==":
		if canNumeric {
			return ln == rn, nil
		}
		return strings.TrimSpace(left) == strings.TrimSpace(right), nil
	case "!=":
		if canNumeric {
			return ln != rn, nil
		}
		return strings.TrimSpace(left) != strings.TrimSpace(right), nil
	case ">":
		if canNumeric {
			return ln > rn, nil
		}
		return left > right, nil
	case "<":
		if canNumeric {
			return ln < rn, nil
		}
		return left < right, nil
	case ">=":
		if canNumeric {
			return ln >= rn, nil
		}
		return left >= right, nil
	case "<=":
		if canNumeric {
			return ln <= rn, nil
		}
		return left <= right, nil
	default:
		return false, fmt.Errorf("unknown operator %q", op)
	}
}

// calcEval evaluates a simple arithmetic expression: + - * /
// Supports integers and floats. Falls back to string concatenation.
func calcEval(expr string, lookup func(string) (string, bool)) (string, error) {
	expr = strings.TrimSpace(expr)
	if expr == "" {
		return "", nil
	}

	// First, resolve any %VAR% references
	expr = resolveCalcVars(expr, lookup)

	// If it's a plain number, return as-is
	if _, err := strconv.ParseFloat(expr, 64); err == nil {
		return expr, nil
	}

	// Try to parse as an arithmetic expression
	// Simple left-to-right evaluation for + - * /
	// Format: num op num [op num ...]
	tokens := tokenizeCalc(expr)
	if len(tokens) == 0 {
		return expr, nil
	}
	if len(tokens) == 1 {
		return tokens[0].val, nil
	}

	// Must have odd number of tokens: val op val op val ...
	if len(tokens)%2 == 0 {
		return expr, nil // not a valid arithmetic expression
	}

	// Evaluate left to right with * and / precedence
	// First pass: * and /
	var pass1 []calcToken
	for i := 0; i < len(tokens); i++ {
		t := tokens[i]
		if t.kind == 'o' && (t.val == "*" || t.val == "/") {
			if len(pass1) == 0 {
				return expr, nil
			}
			left := pass1[len(pass1)-1]
			if left.kind != 'v' {
				return expr, nil
			}
			pass1 = pass1[:len(pass1)-1]
			if i+1 >= len(tokens) || tokens[i+1].kind != 'v' {
				return expr, nil
			}
			right := tokens[i+1]
			lv, errL := strconv.ParseFloat(left.val, 64)
			rv, errR := strconv.ParseFloat(right.val, 64)
			if errL != nil || errR != nil {
				return expr, nil
			}
			var result float64
			if t.val == "*" {
				result = lv * rv
			} else {
				if rv == 0 {
					return "", fmt.Errorf("division by zero")
				}
				result = lv / rv
			}
			pass1 = append(pass1, calcToken{kind: 'v', val: strv(result)})
			i++ // skip right operand
		} else {
			pass1 = append(pass1, t)
		}
	}

	// Second pass: + and -
	result, err := strconv.ParseFloat(pass1[0].val, 64)
	if err != nil {
		return expr, nil
	}
	for i := 1; i < len(pass1); i += 2 {
		if i+1 >= len(pass1) {
			break
		}
		op := pass1[i]
		right := pass1[i+1]
		if op.kind != 'o' || right.kind != 'v' {
			return expr, nil
		}
		rv, err := strconv.ParseFloat(right.val, 64)
		if err != nil {
			return expr, nil
		}
		switch op.val {
		case "+":
			result += rv
		case "-":
			result -= rv
		default:
			return expr, nil
		}
	}
	return strv(result), nil
}

type calcToken struct {
	kind byte // 'v'=value, 'o'=op
	val  string
}

func tokenizeCalc(expr string) []calcToken {
	var out []calcToken
	i := 0
	n := len(expr)
	for i < n {
		ch := expr[i]
		if ch == ' ' || ch == '\t' {
			i++
			continue
		}
		if ch == '+' || ch == '-' || ch == '*' || ch == '/' {
			out = append(out, calcToken{kind: 'o', val: string(ch)})
			i++
			continue
		}
		// Number or identifier
		j := i
		for j < n && expr[j] != ' ' && expr[j] != '\t' && expr[j] != '+' && expr[j] != '-' && expr[j] != '*' && expr[j] != '/' {
			j++
		}
		out = append(out, calcToken{kind: 'v', val: strings.TrimSpace(expr[i:j])})
		i = j
	}
	return out
}

func resolveCalcVars(expr string, lookup func(string) (string, bool)) string {
	var sb strings.Builder
	for p := 0; p < len(expr); p++ {
		ch := expr[p]
		if ch != '%' {
			sb.WriteByte(ch)
			continue
		}
		end := strings.IndexByte(expr[p+1:], '%')
		if end < 0 {
			sb.WriteByte(ch)
			continue
		}
		end += p + 1
		name := expr[p+1 : end]
		if v, ok := lookup(name); ok {
			sb.WriteString(v)
		} else {
			sb.WriteString(expr[p : end+1])
		}
		p = end
	}
	return sb.String()
}

func strv(f float64) string {
	s := strconv.FormatFloat(f, 'f', -1, 64)
	// If no decimal point, FormatFloat won't add one (good).
	return s
}
