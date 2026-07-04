package main

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
)

const indexFileName = "packages.json"

// PackageIndexItem matches the public package index structure.
type PackageIndexItem struct {
	Name     string `json:"name"`
	Version  string `json:"version"`
	Author   string `json:"author"`
	Category string `json:"category,omitempty"`
	URL      string `json:"url"`
	Size     int64  `json:"size"`
	SHA256   string `json:"sha256,omitempty"`
	Filename string `json:"filename,omitempty"`
	Yanked   bool   `json:"yanked,omitempty"`
}

func normalizeIndexCategory(value string) string {
	value = strings.TrimSpace(value)
	if value == "" {
		return ""
	}
	parts := strings.FieldsFunc(value, func(r rune) bool {
		return r == ',' || r == ';' || r == '|' || r == '/'
	})
	if len(parts) > 0 {
		value = strings.TrimSpace(parts[0])
	}
	switch strings.ToLower(value) {
	case "", "uncategorized", "unknown", "none", "null":
		return ""
	default:
		return value
	}
}

func categoryFromRaw(raw map[string]json.RawMessage) string {
	keys := []string{"category", "category_id", "categoryId", "group", "type"}
	for _, key := range keys {
		if value, ok := raw[key]; ok {
			var text string
			if err := json.Unmarshal(value, &text); err == nil {
				if category := normalizeIndexCategory(text); category != "" {
					return category
				}
			}
		}
	}
	arrayKeys := []string{"categories", "tags"}
	for _, key := range arrayKeys {
		if value, ok := raw[key]; ok {
			var list []string
			if err := json.Unmarshal(value, &list); err == nil {
				for _, item := range list {
					if category := normalizeIndexCategory(item); category != "" {
						return category
					}
				}
			}
		}
	}
	return ""
}

func normalizeIndexItems(data []byte) ([]PackageIndexItem, error) {
	var rawItems []map[string]json.RawMessage
	if err := json.Unmarshal(data, &rawItems); err != nil {
		return nil, err
	}

	items := make([]PackageIndexItem, 0, len(rawItems))
	for _, raw := range rawItems {
		encoded, err := json.Marshal(raw)
		if err != nil {
			return nil, err
		}
		var item PackageIndexItem
		if err := json.Unmarshal(encoded, &item); err != nil {
			return nil, err
		}
		if item.Category = normalizeIndexCategory(item.Category); item.Category == "" {
			item.Category = categoryFromRaw(raw)
		}
		items = append(items, item)
	}
	return items, nil
}

func getIndexFilePath() (string, error) {
	exePath, err := os.Executable()
	if err != nil {
		return "", err
	}
	return filepath.Join(filepath.Dir(exePath), indexFileName), nil
}

func updateLocalIndex() error {
	return updateLocalIndexContext(context.Background())
}

func updateLocalIndexContext(ctx context.Context) error {
	if err := ctx.Err(); err != nil {
		return err
	}
	serverURL, err := getServerURL()
	if err != nil {
		return err
	}

	baseURL := strings.TrimRight(serverURL, "/")
	urls := []string{
		fmt.Sprintf("%s/index", baseURL),
		fmt.Sprintf("%s/packages.json", baseURL),
	}
	var resp *http.Response
	for i, url := range urls {
		LogDebug("Updating index from: %s", url)
		req, err := http.NewRequest(http.MethodGet, url, nil)
		if err != nil {
			return err
		}
		req = req.WithContext(ctx)
		req.Header.Set("User-Agent", "GPM-CLI/1.0")
		r, err := http.DefaultClient.Do(req)
		if err != nil {
			return fmt.Errorf("network error: %w", err)
		}
		if r.StatusCode == http.StatusNotFound && i < len(urls)-1 {
			r.Body.Close()
			continue
		}
		if r.StatusCode != http.StatusOK {
			r.Body.Close()
			return fmt.Errorf("server returned status: %d", r.StatusCode)
		}
		resp = r
		break
	}
	if resp == nil {
		return fmt.Errorf("server returned status: %d", http.StatusNotFound)
	}
	defer resp.Body.Close()

	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return err
	}
	if err := ctx.Err(); err != nil {
		return err
	}
	index, err := normalizeIndexItems(data)
	if err != nil {
		return err
	}
	data, err = json.MarshalIndent(index, "", "  ")
	if err != nil {
		return err
	}

	indexPath, err := getIndexFilePath()
	if err != nil {
		return err
	}

	// Create temporary file
	tempFile := indexPath + ".tmp"
	if err := os.WriteFile(tempFile, append(data, '\n'), 0644); err != nil {
		os.Remove(tempFile)
		return err
	}

	// Move temp file to final location
	if err := os.Rename(tempFile, indexPath); err != nil {
		// Try manual remove and rename for Windows
		os.Remove(indexPath)
		if err := os.Rename(tempFile, indexPath); err != nil {
			return err
		}
	}

	PrintLine(T("index_updated"))
	return nil
}

func loadLocalIndex() ([]PackageIndexItem, error) {
	indexPath, err := getIndexFilePath()
	if err != nil {
		return nil, err
	}

	if _, err := os.Stat(indexPath); os.IsNotExist(err) {
		return nil, fmt.Errorf("index not found, please run update")
	}

	data, err := os.ReadFile(indexPath)
	if err != nil {
		return nil, err
	}

	index, err := normalizeIndexItems(data)
	if err != nil {
		return nil, err
	}
	return index, nil
}

func searchLocalIndex(query string, showAll bool, showJson bool) {
	if showJson {
		searchLocalIndexJson(query, showAll)
		return
	}

	index, err := loadLocalIndex()
	if err != nil {
		LogError("Search failed: %v", err)
		PrintLine(T("search_failed_index"))
		return
	}

	query = strings.ToLower(query)

	// Group packages by Author/Name to separate same package name by different authors
	grouped := make(map[string][]PackageIndexItem)
	for _, pkg := range index {
		if strings.Contains(strings.ToLower(pkg.Name), query) ||
			strings.Contains(strings.ToLower(pkg.Author), query) ||
			strings.Contains(strings.ToLower(pkg.Category), query) {
			key := fmt.Sprintf("%s/%s", pkg.Author, pkg.Name)
			grouped[key] = append(grouped[key], pkg)
		}
	}

	if len(grouped) == 0 {
		PrintLine(T("search_no_results"))
		return
	}

	PrintLine(TF("search_header", len(grouped)))

	for key, versions := range grouped {
		parts := strings.Split(key, "/")
		author := parts[0]
		name := parts[1]

		if showAll {
			// Show all versions
			fmt.Printf(" - %s/%s:\n", author, name)
			for _, pkg := range versions {
				fmt.Printf("   v%s\n", pkg.Version)
			}
		} else {
			// Find latest version
			var latest PackageIndexItem
			for i, pkg := range versions {
				if i == 0 || CompareVersions(pkg.Version, latest.Version) > 0 {
					latest = pkg
				}
			}
			fmt.Printf(" - %s/%s (v%s)\n", latest.Author, latest.Name, latest.Version)
		}
	}
}

func listIndex(showAll bool, showJson bool) {
	index, err := loadLocalIndex()
	if err != nil {
		if showJson {
			fmt.Println("[]")
		} else {
			PrintLine(T("search_failed_index"))
		}
		return
	}

	if showJson {
		if showAll {
			encoder := json.NewEncoder(os.Stdout)
			encoder.SetIndent("", "  ")
			encoder.Encode(index)
			return
		}

		grouped := make(map[string][]PackageIndexItem)
		for _, pkg := range index {
			key := fmt.Sprintf("%s/%s", pkg.Author, pkg.Name)
			grouped[key] = append(grouped[key], pkg)
		}

		var results []PackageIndexItem
		for _, versions := range grouped {
			var latest PackageIndexItem
			for i, pkg := range versions {
				if i == 0 || CompareVersions(pkg.Version, latest.Version) > 0 {
					latest = pkg
				}
			}
			results = append(results, latest)
		}

		encoder := json.NewEncoder(os.Stdout)
		encoder.SetIndent("", "  ")
		encoder.Encode(results)
		return
	}

	grouped := make(map[string][]PackageIndexItem)
	for _, pkg := range index {
		key := fmt.Sprintf("%s/%s", pkg.Author, pkg.Name)
		grouped[key] = append(grouped[key], pkg)
	}

	PrintLine(TF("search_header", len(grouped)))
	for key, versions := range grouped {
		parts := strings.Split(key, "/")
		author := parts[0]
		name := parts[1]

		if showAll {
			fmt.Printf(" - %s/%s:\n", author, name)
			for _, pkg := range versions {
				fmt.Printf("   v%s\n", pkg.Version)
			}
		} else {
			var latest PackageIndexItem
			for i, pkg := range versions {
				if i == 0 || CompareVersions(pkg.Version, latest.Version) > 0 {
					latest = pkg
				}
			}
			fmt.Printf(" - %s/%s (v%s)\n", latest.Author, latest.Name, latest.Version)
		}
	}
}

func searchLocalIndexJson(query string, showAll bool) {
	index, err := loadLocalIndex()
	if err != nil {
		// Output empty array on error for JSON mode
		fmt.Println("[]")
		return
	}

	query = strings.ToLower(query)

	var results []PackageIndexItem

	// Group packages to handle latest version logic if not showAll
	grouped := make(map[string][]PackageIndexItem)

	for _, pkg := range index {
		if strings.Contains(strings.ToLower(pkg.Name), query) ||
			strings.Contains(strings.ToLower(pkg.Author), query) ||
			strings.Contains(strings.ToLower(pkg.Category), query) {

			if showAll {
				results = append(results, pkg)
			} else {
				key := fmt.Sprintf("%s/%s", pkg.Author, pkg.Name)
				grouped[key] = append(grouped[key], pkg)
			}
		}
	}

	if !showAll {
		// Filter for latest versions
		for _, versions := range grouped {
			var latest PackageIndexItem
			for i, pkg := range versions {
				if i == 0 || CompareVersions(pkg.Version, latest.Version) > 0 {
					latest = pkg
				}
			}
			results = append(results, latest)
		}
	}

	// Output JSON
	encoder := json.NewEncoder(os.Stdout)
	encoder.SetIndent("", "  ")
	if err := encoder.Encode(results); err != nil {
		fmt.Println("[]")
	}
}

func findPackageInIndex(input string) (*PackageIndexItem, error) {
	index, err := loadLocalIndex()
	if err != nil {
		return nil, err
	}

	// Parse input for version and author (Author/Name@Version or Name@Version)
	var author, name, version string

	// Handle @Version
	partsAt := strings.Split(input, "@")
	base := partsAt[0]
	if len(partsAt) > 1 {
		version = partsAt[1]
	}

	// Handle Author/Name
	partsSlash := strings.Split(base, "/")
	if len(partsSlash) > 1 {
		author = partsSlash[0]
		name = partsSlash[1]
	} else {
		name = base
	}

	var candidates []PackageIndexItem
	for _, pkg := range index {
		if strings.EqualFold(pkg.Name, name) {
			// If author is specified, match it
			if author != "" && !strings.EqualFold(pkg.Author, author) {
				continue
			}
			candidates = append(candidates, pkg)
		}
	}

	if len(candidates) == 0 {
		return nil, fmt.Errorf("package '%s' not found", base)
	}

	if version != "" {
		// Find exact version
		for _, pkg := range candidates {
			if pkg.Version == version {
				return &pkg, nil
			}
		}
		return nil, fmt.Errorf("version '%s' of package '%s' not found", version, name)
	}

	// Find latest version
	var latest PackageIndexItem
	for i, pkg := range candidates {
		if i == 0 || CompareVersions(pkg.Version, latest.Version) > 0 {
			latest = pkg
		}
	}

	return &latest, nil
}
