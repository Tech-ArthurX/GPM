package main

import "unicode"

func compareVersionsNumeric(a, b string) int {
	aa := splitVersionDigits(a)
	bb := splitVersionDigits(b)
	n := len(aa)
	if len(bb) > n {
		n = len(bb)
	}
	for i := 0; i < n; i++ {
		av, bv := 0, 0
		if i < len(aa) {
			av = aa[i]
		}
		if i < len(bb) {
			bv = bb[i]
		}
		if av < bv {
			return -1
		}
		if av > bv {
			return 1
		}
	}
	return 0
}

func splitVersionDigits(v string) []int {
	var out []int
	current := 0
	have := false
	for _, r := range v {
		if unicode.IsDigit(r) {
			current = current*10 + int(r-'0')
			have = true
			continue
		}
		if have {
			out = append(out, current)
			current = 0
			have = false
		}
	}
	if have {
		out = append(out, current)
	}
	return out
}
