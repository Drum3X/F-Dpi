// Package fdpidns sets the per-link DNS of systemd-resolved for F-Dpi's Automatic DNS while DPI Bypass runs (Linux).
//
// The GUI runs "ThroneCore --fdpi-dns" (the setuid core, parent-checked) with one JSON request on stdin and reads one JSON
// reply from stdout. It keeps the backup and decides what to restore (src/sys/linux/SystemDns_linux.cpp); this side only
// reads and writes link settings, so F-Dpi -cleanup can use it without a running core.
package fdpidns

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
)

// Link is one interface's resolved settings. Servers are "addr", "addr#name" or "[v6]:port#name" (resolvectl's syntax).
type Link struct {
	Ifindex      int32    `json:"ifindex"`
	Servers      []string `json:"servers"`
	DNSOverTLS   string   `json:"dns_over_tls"` // "yes", "no", "opportunistic" or "" (resolved's default)
	Domains      []string `json:"domains"`      // "~example.com" is routing-only
	DefaultRoute *bool    `json:"default_route,omitempty"`
}

type request struct {
	Op    string `json:"op"` // "get", "set", "flush", "supported"
	Links []Link `json:"links"`
}

type reply struct {
	Links     []Link `json:"links,omitempty"`
	Supported bool   `json:"supported"`
	Error     string `json:"error,omitempty"`
}

// Main runs one request and exits.
func Main(stdin io.Reader, stdout io.Writer) int {
	var in request
	out := reply{}
	if err := json.NewDecoder(io.LimitReader(stdin, 1<<20)).Decode(&in); err != nil {
		out.Error = fmt.Sprintf("bad request: %v", err)
	} else {
		var err error
		switch in.Op {
		case "supported":
			out.Supported = supported()
		case "get":
			out.Links, err = get(in.Links)
		case "set":
			err = set(in.Links)
		case "flush":
			err = flush()
		default:
			err = fmt.Errorf("unknown op %q", in.Op)
		}
		if err != nil {
			out.Error = err.Error()
		}
	}
	_ = json.NewEncoder(stdout).Encode(out)
	if out.Error != "" {
		return 1
	}
	return 0
}

// Run is main()'s "--fdpi-dns".
func Run() {
	os.Exit(Main(os.Stdin, os.Stdout))
}
