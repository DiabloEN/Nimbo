package main

import "slices"

func validChannelIcon(icon string) bool {
	return slices.Contains([]string{"", "hash", "sound", "headphones", "mic", "chat", "music", "game", "coffee", "home", "users", "star", "heart", "book", "code", "cinema", "radio"}, icon)
}
