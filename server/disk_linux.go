package main

import "syscall"

func diskSpace(path string) (uint64, uint64, error) {
	var s syscall.Statfs_t
	err := syscall.Statfs(path, &s)
	return s.Bavail * uint64(s.Bsize), s.Blocks * uint64(s.Bsize), err
}
