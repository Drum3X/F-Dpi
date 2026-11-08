// Package dpi runs F-Dpi's DPI Bypass engine on Linux: zapret's nfqws behind an nftables NFQUEUE rule.
//
// The GUI runs unprivileged; this (setuid root) core starts nfqws for it. nfqws reads the files its options name
// and chowns the autohostlist file while it is still root (zapret nfq/nfqws.c: before droproot), so the core never
// hands it a path the user chose: every file option is read here, under rules that give the user no file they
// could not read already, and copied into a runtime directory of root's that nfqws gets instead.
package dpi

import (
	"errors"
	"fmt"
	"strings"
)

// Options F-Dpi sets itself, or that would make nfqws write files, fork away or read another configuration.
var managedOptions = map[string]string{
	"qnum":                "the core picks the queue",
	"uid":                 "the core drops nfqws' privileges itself",
	"user":                "the core drops nfqws' privileges itself",
	"gid":                 "the core drops nfqws' privileges itself",
	"dpi-desync-fwmark":   "the core sets the mark its nftables rule skips",
	"daemon":              "nfqws must stay a child of the core",
	"pidfile":             "writes a file",
	"hostlist-auto-debug": "writes a file",
	"wf-save":             "writes a file",
}

// WinDivert (winws) options: the nftables rule picks the traffic on Linux.
var windivertOptions = []string{"wf-tcp", "wf-udp", "wf-l3", "wf-raw", "wf-raw-part", "wf-iface", "wf-dup-check", "wf-filter-lan", "wf-filter-loopback", "wf-tcp-in", "wf-tcp-out", "wf-udp-in", "wf-udp-out", "wf-tcp-empty"}

// Options whose value is one file.
var fileOptions = map[string]bool{
	"hostlist":         true,
	"hostlist-exclude": true,
	"hostlist-auto":    true,
	"ipset":            true,
	"ipset-exclude":    true,
}

// Prefixes of options whose value may be a file (the fake payloads take a file, 0x<hex> or a ! default).
var filePrefixes = []string{"dpi-desync-fake-", "dpi-desync-split-seqovl-pattern", "dpi-desync-fsplit-pattern"}

// FileArg is an option whose file the core copies into the runtime directory.
type FileArg struct {
	Index  int    // in Args
	Option string // without the dashes
	Path   string // absolute, as the GUI gave it
}

// Parsed is a validated nfqws command line.
type Parsed struct {
	Args  []string
	Files []FileArg
	// Indexes in Files of the --hostlist-auto options; every --new profile has one, all naming the same file.
	AutoHostlist []int
	Incoming     bool // needs the server's first packets too (autohostlist, autottl)
}

func optionName(arg string) (name, value string, hasValue bool) {
	body := strings.TrimPrefix(arg, "--")
	if i := strings.IndexByte(body, '='); i >= 0 {
		return body[:i], body[i+1:], true
	}
	return body, "", false
}

func isFileOption(name string) bool {
	if fileOptions[name] {
		return true
	}
	for _, prefix := range filePrefixes {
		if strings.HasPrefix(name, prefix) {
			return true
		}
	}
	return false
}

// Parse validates the GUI's nfqws options (without --qnum, --uid and --dpi-desync-fwmark, which the core adds).
func Parse(args []string) (*Parsed, error) {
	if len(args) == 0 {
		return nil, errors.New("no nfqws options")
	}
	out := &Parsed{}
	for i, arg := range args {
		if strings.HasPrefix(arg, "@") {
			return nil, fmt.Errorf("%q: an option file cannot be used", arg)
		}
		if !strings.HasPrefix(arg, "--") || len(arg) < 3 {
			return nil, fmt.Errorf("%q is not an nfqws option", arg)
		}
		if strings.ContainsAny(arg, "\x00\n\r") {
			return nil, fmt.Errorf("%q contains a control character", arg)
		}
		name, value, hasValue := optionName(arg)
		if why, ok := managedOptions[name]; ok {
			return nil, fmt.Errorf("--%s cannot be used: %s", name, why)
		}
		for _, wf := range windivertOptions {
			if name == wf {
				return nil, fmt.Errorf("--%s is a WinDivert (Windows) option; on Linux the TCP/UDP ports pick the traffic", name)
			}
		}
		if name == "debug" && hasValue && (strings.HasPrefix(value, "@") || strings.HasPrefix(value, "/") || value == "syslog") {
			return nil, errors.New("--debug may only print to the log (--debug or --debug=1)")
		}
		if hasValue && strings.Contains(value, "..") && strings.Contains(value, "/") {
			return nil, fmt.Errorf("--%s: a path may not contain ..", name)
		}
		if name == "hostlist-auto" || strings.Contains(value, "autottl") || strings.HasPrefix(name, "dpi-desync-autottl") {
			out.Incoming = true
		}
		if hasValue && strings.HasPrefix(value, "/") {
			if !isFileOption(name) {
				return nil, fmt.Errorf("--%s does not take a file", name)
			}
			out.Files = append(out.Files, FileArg{Index: i, Option: name, Path: value})
			if name == "hostlist-auto" {
				out.AutoHostlist = append(out.AutoHostlist, len(out.Files)-1)
			}
		} else if name == "hostlist-auto" {
			return nil, errors.New("--hostlist-auto needs an absolute path")
		}
		out.Args = append(out.Args, arg)
	}
	return out, nil
}

// ParsePorts checks nftables port specs: "443", "80,443", "50000-50100".
func ParsePorts(specs []string) ([][2]uint16, error) {
	var out [][2]uint16
	for _, spec := range specs {
		for _, part := range strings.Split(spec, ",") {
			part = strings.TrimSpace(part)
			if part == "" {
				continue
			}
			from, to, isRange := strings.Cut(part, "-")
			a, err := parsePort(from)
			if err != nil {
				return nil, err
			}
			b := a
			if isRange {
				if b, err = parsePort(to); err != nil {
					return nil, err
				}
			}
			if b < a {
				return nil, fmt.Errorf("port range %q is reversed", part)
			}
			out = append(out, [2]uint16{a, b})
		}
	}
	return out, nil
}

func parsePort(s string) (uint16, error) {
	var n int
	if _, err := fmt.Sscanf(strings.TrimSpace(s), "%d", &n); err != nil || n < 1 || n > 65535 || fmt.Sprint(n) != strings.TrimSpace(s) {
		return 0, fmt.Errorf("%q is not a port", s)
	}
	return uint16(n), nil
}
