package main

import (
	"strings"
	"unicode/utf16"
)

// Each byte styles one UTF-16 unit. Trailing plain text needs no entry.
// This field contains no markup and never changes the original message text.
func messageFormat(text, format string, leading int) (string, error) {
	if format == "" {
		return "", nil
	}
	size := len(utf16.Encode([]rune(text)))
	if len(format) > size || strings.Trim(format, "012348") != "" {
		return "", fail(400, "Некорректный формат сообщения")
	}
	if leading >= len(format) {
		return "", nil
	}
	return strings.TrimRight(format[leading:], "0"), nil
}
