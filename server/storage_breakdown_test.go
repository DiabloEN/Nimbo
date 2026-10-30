package main

import "testing"

func TestStorageBreakdown(t *testing.T) {
	photo := fileAttachment{ID: randomKey(), Name: "photo.JPG", Size: 1000}
	d := workspaceData{Messages: map[string][]chatMessage{"general": {
		{ID: "one", Text: "Привет", Attachment: &photo},
		{ID: "two", Attachments: []fileAttachment{photo, {ID: randomKey(), Name: "movie.mp4", Size: 2000}, {ID: randomKey(), Name: "song.mp3", Size: 3000}, {ID: randomKey(), Name: "script.html", Size: 4000}}},
	}}}
	b := storageBreakdown(&d)
	if b["photos"] != 1000 || b["video"] != 2000 || b["audio"] != 3000 || b["files"] != 4000 {
		t.Fatalf("wrong classification or duplicate accounting: %v", b)
	}
	var total int64
	for _, value := range b {
		if value < 0 {
			t.Fatal("negative bytes")
		}
		total += value
	}
	if total != dataSize(&d) || b["messages"] == 0 || b["other"] == 0 {
		t.Fatalf("breakdown does not reconcile: %v total=%d", b, total)
	}
}
