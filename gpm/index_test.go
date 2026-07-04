package main

import "testing"

func TestNormalizeIndexItemsCategoryAliases(t *testing.T) {
	data := []byte(`[
		{"name":"A","version":"1.0","author":"ArthurX","category":"Archive"},
		{"name":"B","version":"1.0","author":"ArthurX","categories":["System","Utility"]},
		{"name":"C","version":"1.0","author":"ArthurX","tags":["Editor"]},
		{"name":"D","version":"1.0","author":"ArthurX","type":"Search"},
		{"name":"E","version":"1.0","author":"ArthurX","group":"Driver"}
	]`)

	items, err := normalizeIndexItems(data)
	if err != nil {
		t.Fatalf("normalizeIndexItems: %v", err)
	}

	want := []string{"Archive", "System", "Editor", "Search", "Driver"}
	if len(items) != len(want) {
		t.Fatalf("item count = %d, want %d", len(items), len(want))
	}
	for i, item := range items {
		if item.Category != want[i] {
			t.Fatalf("item %d category = %q, want %q", i, item.Category, want[i])
		}
	}
}
