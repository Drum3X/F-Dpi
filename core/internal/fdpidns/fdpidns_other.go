//go:build !linux

package fdpidns

import "errors"

var errUnsupported = errors.New("systemd-resolved is Linux only")

func supported() bool            { return false }
func get([]Link) ([]Link, error) { return nil, errUnsupported }
func set([]Link) error           { return errUnsupported }
func flush() error               { return errUnsupported }
