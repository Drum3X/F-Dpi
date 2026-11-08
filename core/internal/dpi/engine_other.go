//go:build !linux

package dpi

import "errors"

var errUnsupported = errors.New("the core runs DPI Bypass only on Linux; elsewhere the GUI runs it itself")

func Start(StartRequest) error { return errUnsupported }

func Stop() Status { return Status{} }

func Query(afterSeq, autoVersion int64) Status { return Status{} }

func CleanupAtBoot() {}

func RunGuard() {}
