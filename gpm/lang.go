package main

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

var langStrings map[string]string

func InitLang() {
	lang := strings.TrimSpace(os.Getenv("GPM_LANG"))
	if lang == "" {
		lang = strings.TrimSpace(os.Getenv("HPM_LANG"))
	}
	if lang == "" {
		lang = "zh-CN"
	}
	langDir := strings.TrimSpace(os.Getenv("GPM_LANG_DIR"))
	if langDir == "" {
		langDir = strings.TrimSpace(os.Getenv("HPM_LANG_DIR"))
	}
	fallback := loadLangFile("en-US")
	selected := loadLangFileWithDir(lang, langDir)
	langStrings = map[string]string{}
	for k, v := range fallback {
		langStrings[k] = v
	}
	for k, v := range selected {
		langStrings[k] = v
	}
}

func T(key string) string {
	if langStrings != nil {
		if val, ok := langStrings[key]; ok && val != "" {
			return val
		}
	}
	return key
}

func TF(key string, args ...interface{}) string {
	return fmt.Sprintf(T(key), args...)
}

func loadLangFile(lang string) map[string]string {
	return loadLangFileWithDir(lang, "")
}

func loadLangFileWithDir(lang string, langDir string) map[string]string {
	result := map[string]string{}
	if langDir != "" {
		path := filepath.Join(langDir, lang+".ini")
		if tryLoadLang(path, result) {
			return result
		}
	}
	exePath, err := os.Executable()
	if err != nil {
		return result
	}
	exeDir := filepath.Dir(exePath)
	path := filepath.Join(exeDir, "lang", lang+".ini")
	if tryLoadLang(path, result) {
		return result
	}
	if cwd, err := os.Getwd(); err == nil {
		path = filepath.Join(cwd, "lang", lang+".ini")
		_ = tryLoadLang(path, result)
	}
	return result
}

func tryLoadLang(path string, result map[string]string) bool {
	file, err := os.Open(path)
	if err != nil {
		return false
	}
	defer file.Close()
	section := ""
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		if strings.HasPrefix(line, ";") || strings.HasPrefix(line, "#") {
			continue
		}
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			section = strings.ToLower(strings.TrimSpace(line[1 : len(line)-1]))
			continue
		}
		if section != "strings" {
			continue
		}
		parts := strings.SplitN(line, "=", 2)
		if len(parts) != 2 {
			continue
		}
		key := strings.TrimSpace(parts[0])
		val := strings.TrimSpace(parts[1])
		if key != "" {
			result[key] = val
		}
	}
	return true
}
