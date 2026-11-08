package dpi

import (
	"strings"
	"testing"
)

func TestParseAcceptsStrategy(t *testing.T) {
	p, err := Parse([]string{
		"--filter-tcp=443", "--dpi-desync=fake,multidisorder", "--dpi-desync-fooling=md5sig",
		"--dpi-desync-fake-quic=/opt/F-Dpi/zapret/files/quic_initial_www_google_com.bin",
		"--hostlist=/home/u/.config/F-Dpi/config/dpi/hostlist-1.txt",
		"--new", "--filter-udp=443", "--hostlist-auto=/home/u/.config/F-Dpi/config/dpi/autohostlist-1.txt",
		"--ipset-ip=10.0.0.0/8", "--dpi-desync-fake-tls=0x1603",
	})
	if err != nil {
		t.Fatal(err)
	}
	if len(p.Files) != 3 || len(p.AutoHostlist) != 1 || !p.Incoming {
		t.Fatalf("files %v auto %v incoming %v", p.Files, p.AutoHostlist, p.Incoming)
	}
	if p.Files[0].Index != 3 || p.Files[0].Option != "dpi-desync-fake-quic" {
		t.Fatalf("first file %+v", p.Files[0])
	}
}

func TestParseRefuses(t *testing.T) {
	for _, args := range [][]string{
		{"@/etc/nfqws.conf"},
		{"--qnum=1"},
		{"--uid=0"},
		{"--daemon"},
		{"--pidfile=/tmp/x"},
		{"--wf-tcp=443"},
		{"--debug=@/root/log"},
		{"--hostlist-auto-debug=/tmp/x"},
		{"--filter-tcp=/etc/shadow"},
		{"--hostlist=../../etc/shadow"},
		{"--hostlist-auto=relative.txt"},
		{"plain"},
	} {
		if _, err := Parse(args); err == nil {
			t.Errorf("%v accepted", args)
		}
	}
}

func TestParsePorts(t *testing.T) {
	got, err := ParsePorts([]string{"80,443", "50000-50100"})
	if err != nil || len(got) != 3 || got[2] != [2]uint16{50000, 50100} {
		t.Fatalf("%v %v", got, err)
	}
	for _, bad := range []string{"0", "65536", "443-80", "x", "1e3"} {
		if _, err := ParsePorts([]string{bad}); err == nil || !strings.Contains(err.Error(), bad[:1]) {
			t.Errorf("%q accepted (%v)", bad, err)
		}
	}
}
