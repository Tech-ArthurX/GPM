package main

import (
	"strconv"
	"strings"
)

// CompareVersions compares two version strings.
// Returns:
//
//	-1 if v1 < v2
//	 0 if v1 == v2
//	 1 if v1 > v2
func CompareVersions(v1, v2 string) int {
	p1 := parseVersion(v1)
	p2 := parseVersion(v2)

	len1 := len(p1)
	len2 := len(p2)
	maxLen := len1
	if len2 > maxLen {
		maxLen = len2
	}

	for i := 0; i < maxLen; i++ {
		var n1, n2 int
		if i < len1 {
			n1 = p1[i]
		}
		if i < len2 {
			n2 = p2[i]
		}

		if n1 < n2 {
			return -1
		}
		if n1 > n2 {
			return 1
		}
	}

	return 0
}

func parseVersion(v string) []int {
	v = strings.TrimPrefix(v, "v")
	parts := strings.Split(v, ".")
	var nums []int
	for _, p := range parts {
		// Handle things like "1.0.1-beta" -> just take "1"
		// Simple approach: take leading digits
		var digits string
		for _, c := range p {
			if c >= '0' && c <= '9' {
				digits += string(c)
			} else {
				break
			}
		}
		if digits == "" {
			nums = append(nums, 0)
		} else {
			n, _ := strconv.Atoi(digits)
			nums = append(nums, n)
		}
	}
	return nums
}
