package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

var DebugMode bool
var logFile *os.File

func SetDebug(debug bool) {
	DebugMode = debug
}

func InitLogging() error {
	exePath, err := os.Executable()
	if err != nil {
		return err
	}
	exeDir := filepath.Dir(exePath)
	logDir := filepath.Join(exeDir, "logs")
	if err := os.MkdirAll(logDir, 0755); err != nil {
		return err
	}
	fileName := "gpm-" + time.Now().Format("20060102-150405") + ".log"
	logPath := filepath.Join(logDir, fileName)
	file, err := os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		return err
	}
	logFile = file
	return nil
}

func CloseLogging() {
	if logFile != nil {
		logFile.Close()
	}
}

func writeLog(prefix, format string, v ...interface{}) {
	if logFile == nil {
		return
	}
	line := fmt.Sprintf(prefix+format, v...)
	if !strings.HasSuffix(line, "\n") {
		line += "\n"
	}
	logFile.WriteString(line)
}

// LogDebug prints logs only when DebugMode is true
func LogDebug(format string, v ...interface{}) {
	writeLog("[DEBUG] ", format, v...)
	if DebugMode {
		fmt.Printf("[DEBUG] "+format+"\n", v...)
	}
}

// LogError prints error logs. In quiet mode, these might be suppressed or shown depending on design.
// User asked for "success x, failed x", so detailed errors should probably be Debug only, 
// unless it's a critical crash.
func LogError(format string, v ...interface{}) {
	writeLog("[ERROR] ", format, v...)
	if DebugMode {
		fmt.Printf("[ERROR] "+format+"\n", v...)
	}
}

// PrintAlways prints messages that must be seen (like prompts or final summary)
func PrintAlways(format string, v ...interface{}) {
	msg := fmt.Sprintf(format, v...)
	fmt.Print(msg)
	if logFile != nil {
		logFile.WriteString(msg)
	}
}

func PrintText(text string) {
	PrintAlways("%s", text)
}

func PrintLine(text string) {
	PrintAlways("%s\n", text)
}
