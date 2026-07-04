package main

import (
	"embed"
	"flag"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"strings"

	"github.com/SECTL/GPM/gs"
)

//go:embed res
var resFS embed.FS

func main() {
	os.Exit(Run(os.Args[1:]))
}

func Run(args []string) int {
	InitLang()

	flags := flag.NewFlagSet("gpm-cli", flag.ContinueOnError)
	flags.SetOutput(io.Discard)
	installPath := flags.String("i", "", "")
	list := flags.Bool("l", false, "")
	remove := flags.String("r", "", "")
	update := flags.Bool("u", false, "")
	search := flags.String("s", "", "")
	indexList := flags.Bool("index", false, "")
	showAll := flags.Bool("all", false, "")
	showJson := flags.Bool("j", false, "")
	debug := flags.Bool("d", false, "")
	autoConfirm := flags.Bool("y", false, "")
	showVersion := flags.Bool("version", false, "")
	packageVersion := flags.String("v", "", "")
	threads := flags.Int("t", defaultDownloadThreads, "")
	guiMode := flags.Bool("gui", false, "")
	port := flags.String("port", "8080", "")
	noGUISpawn := flags.Bool("no-gui-spawn", false, "")
	guiClient := flags.String("gui-client", "", "")
	showGsSyntax := flags.Bool("gs-syntax", false, "")

	flags.Usage = func() {
		PrintLine(TF("usage_header", filepath.Base(os.Args[0])))
		PrintLine(T("usage_install"))
		PrintLine(T("usage_list"))
		PrintLine(T("usage_remove"))
		PrintLine(T("usage_update"))
		PrintLine(T("usage_search"))
		PrintLine(T("usage_all")) // New
		PrintLine(T("usage_debug"))
		PrintLine(T("usage_yes"))
		PrintLine(T("usage_version"))
		PrintLine(T("usage_package_version"))
		PrintLine(T("usage_threads"))
		PrintLine(T("usage_gs_syntax"))
		PrintLine("  -no-gui-spawn        Do not auto-launch the GUI client (gpm-fluent)")
		PrintLine("  -gui-client <path>   Override GUI client path (default: gpm-fluent-new.exe)")
	}

	if err := flags.Parse(args); err != nil {
		flags.Usage()
		return 1
	}

	if err := InitLogging(); err != nil {
		PrintLine(T("log_init_failed"))
	} else {
		defer CloseLogging()
	}

	SetDebug(*debug)

	if *showVersion {
		PrintVersion()
		return 0
	}

	if *showGsSyntax {
		PrintLine(gs.SyntaxText())
		return 0
	}

	exeBase := strings.ToLower(filepath.Base(os.Args[0]))
	if *guiMode || strings.Contains(exeBase, "gpm-gui") {
		return startGUIServer(*port, *debug, *noGUISpawn, *guiClient)
	}

	// List packages
	if *list {
		listInstalledPackages(*showJson)
		return 0
	}

	// Update local index
	if *update {
		if err := updateLocalIndex(); err != nil {
			LogError("Update failed: %v", err)
			PrintLine(T("update_failed"))
			return 1
		}
		PrintLine(T("update_success"))
		return 0
	}

	// List index
	if *indexList {
		listIndex(*showAll, *showJson)
		return 0
	}

	// Search packages
	if *search != "" {
		searchLocalIndex(*search, *showAll, *showJson)
		return 0
	}

	// Uninstall package
	if *remove != "" {
		if err := uninstall(*remove, *autoConfirm); err != nil {
			LogError("Uninstall failed: %v", err)
			PrintLine(T("uninstall_failed"))
			return 1
		}
		return 0
	}

	if *installPath == "" {
		flags.Usage()
		return 1
	}

	// Extract embedded resources to temp directory
	tempDir, err := extractResources()
	if err != nil {
		LogError("Failed to extract resources: %v", err)
		PrintLine(T("init_failed"))
		return 1
	}
	defer os.RemoveAll(tempDir) // Ensure temp directory is cleaned up

	// Check if installPath is a URL
	if strings.HasPrefix(strings.ToLower(*installPath), "http://") || strings.HasPrefix(strings.ToLower(*installPath), "https://") {
		LogDebug("Detected URL, starting download...")
		downloadDest := filepath.Join(tempDir, "downloaded_package.gpm")

		downloader := NewDownloader(*installPath, downloadDest, *threads, "GPM-CLI/1.0")
		downloader.OnProgress = cliDownloadProgress
		if err := downloader.Start(); err != nil {
			LogError("Download failed: %v", err)
			return 1
		}
		*installPath = downloadDest
	} else if _, err := os.Stat(*installPath); os.IsNotExist(err) {
		// Not a local file, assume it's a package name

		// Apply version if specified via -v flag
		if *packageVersion != "" && !strings.Contains(*installPath, "@") {
			*installPath = *installPath + "@" + *packageVersion
		}

		LogDebug("Resolving package: %s", *installPath)

		var info *RemotePackageInfo

		// 1. Try Local Index first
		localPkg, err := findPackageInIndex(*installPath)
		if err == nil && localPkg != nil {
			LogDebug("Found in local index.")
			info = &RemotePackageInfo{
				Name:     localPkg.Name,
				Version:  localPkg.Version,
				Author:   localPkg.Author,
				Category: localPkg.Category,
				URL:      localPkg.URL,
				Size:     localPkg.Size,
				SHA256:   localPkg.SHA256,
				Filename: localPkg.Filename,
			}
		} else {
			// 2. Fallback to remote fetch (optional, but good for UX if user forgot to update)
			LogDebug("Not found in local index, trying remote fetch...")
			info, err = fetchPackageInfo(*installPath)
			if err != nil {
				LogError("Failed to resolve package: %v", err)
				PrintLine(T("install_resolve_failed")) // "Please run -u to update index or check package name"
				return 1
			}
		}

		LogDebug("Found package: %s (v%s) by %s", info.Name, info.Version, info.Author)
		if localPkg != nil && localPkg.Yanked {
			LogError("Package '%s' v%s has been yanked by the operator and cannot be installed.", info.Name, info.Version)
			PrintLine(TF("package_yanked", info.Name, info.Version))
			return 1
		}
		downloadFileName, err := packageDownloadFileName(info.Name)
		if err != nil {
			LogError("Unsafe package name: %v", err)
			return 1
		}
		downloadDest := filepath.Join(tempDir, downloadFileName)

		// Use info.URL for download.
		downloader := NewDownloader(info.URL, downloadDest, *threads, "GPM-CLI/1.0")
		downloader.OnProgress = cliDownloadProgress
		if err := downloader.Start(); err != nil {
			LogError("Download failed: %v", err)
			return 1
		}
		if err := verifyFileSHA256(downloadDest, info.SHA256); err != nil {
			LogError("Downloaded package integrity check failed: %v", err)
			return 1
		}

		*installPath = downloadDest

	}

	successCount := 0
	failCount := 0

	success, err := install(*installPath, tempDir, *autoConfirm)
	if err != nil {
		failCount++
		// If debug is on, we see the error in LogDebug inside install or here
		LogDebug("Installation failed: %v", err)
	} else {
		if success {
			successCount++
		} else {
			// Skipped (user said No to overwrite)
		}
	}

	if !*debug {
		PrintLine(TF("install_summary", successCount, failCount))
	}
	return 0
}

func cliDownloadProgress(update DownloadProgressUpdate) {
	if !DebugMode {
		return
	}
	if update.Total > 0 {
		LogDebug("Download progress: %d%% (%d/%d bytes, threads=%d)", update.Percent, update.Downloaded, update.Total, update.Threads)
		return
	}
	LogDebug("Download progress: %d bytes", update.Downloaded)
}

func PrintVersion() {
	PrintLine(TF("version_format", "1.0.0"))
	PrintLine(TF("producer_format", "ArthurX"))
}

func extractResources() (string, error) {
	tempDir, err := os.MkdirTemp("", "gpm-cli-res")
	if err != nil {
		return "", err
	}

	err = fs.WalkDir(resFS, ".", func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}

		destPath := filepath.Join(tempDir, path)

		if d.IsDir() {
			return os.MkdirAll(destPath, 0755)
		}

		content, err := resFS.ReadFile(path)
		if err != nil {
			return err
		}

		return os.WriteFile(destPath, content, 0755)
	})

	if err != nil {
		os.RemoveAll(tempDir)
		return "", err
	}

	return tempDir, nil
}
