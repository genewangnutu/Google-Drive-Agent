// Package main is required by cgo's -buildmode=c-archive: the archive still
// needs a main package with a main function, even though the real process
// entry point is the C++ main() that links against libgodrive.a.
package main

func main() {}
