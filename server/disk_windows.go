package main

import (
	"syscall"
	"unsafe"
)

func diskSpace(path string) (uint64, uint64, error) {
	p, e := syscall.UTF16PtrFromString(path)
	if e != nil {
		return 0, 0, e
	}
	var free, total, unused uint64
	r, _, e := syscall.NewLazyDLL("kernel32.dll").NewProc("GetDiskFreeSpaceExW").Call(uintptr(unsafe.Pointer(p)), uintptr(unsafe.Pointer(&free)), uintptr(unsafe.Pointer(&total)), uintptr(unsafe.Pointer(&unused)))
	if r == 0 {
		return 0, 0, e
	}
	return free, total, nil
}
