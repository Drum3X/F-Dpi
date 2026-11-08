//go:build linux

package dpi

// A root-only end-to-end check of the engine on this machine's network: nftables owner table, nfqws, file staging, stop.
//
//	go test -c -o /tmp/dpi.test ./internal/dpi/
//	sudo FDPI_ROOT_TEST=1 FDPI_TEST_ZAPRET=<repo>/deps/linux/zapret /tmp/dpi.test -test.run Root -test.v
//
// FDPI_TEST_SITE (default discord.com) should be blocked on the network for "before" and "with DPI" to differ;
// FDPI_TEST_STRATEGY overrides the nfqws options (space separated). It always removes what it set up.

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"testing"
	"time"

	"github.com/sagernet/nftables"
)

// The guard re-runs the test binary as "dpi.test --fdpi-nfqws <nfqws> ..." (spawn uses os.Executable).
func TestMain(m *testing.M) {
	if len(os.Args) > 2 && os.Args[1] == guardArg {
		// The guard's environment is empty: the test's zapret folder is the one holding the nfqws it was given.
		zapretDirForTest = filepath.Dir(os.Args[2])
		RunGuard()
	}
	os.Exit(m.Run())
}

func rootTestFetch(site string) string {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, http.MethodGet, "https://"+site+"/", nil)
	client := &http.Client{CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}
	resp, err := client.Do(req)
	if err != nil {
		return "FAIL " + err.Error()
	}
	defer resp.Body.Close()
	_, _ = io.Copy(io.Discard, io.LimitReader(resp.Body, 1<<16))
	return fmt.Sprintf("OK %d", resp.StatusCode)
}

func rootTestTablePresent(t *testing.T) bool {
	conn, err := nftables.New()
	if err != nil {
		t.Fatalf("nftables: %v", err)
	}
	tables, err := conn.ListTablesOfFamily(nftables.TableFamilyINet)
	if err != nil {
		t.Fatalf("list tables: %v", err)
	}
	for _, table := range tables {
		if table.Name == fmt.Sprintf("fdpi-%d", os.Getpid()) {
			return true
		}
	}
	return false
}

func rootTestCopy(t *testing.T, src, dst string, mode os.FileMode) {
	data, err := os.ReadFile(src)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(dst, data, mode); err != nil {
		t.Fatal(err)
	}
}

func TestRootEngine(t *testing.T) {
	if os.Getenv("FDPI_ROOT_TEST") != "1" || os.Geteuid() != 0 {
		t.Skip("needs root and FDPI_ROOT_TEST=1")
	}
	src := os.Getenv("FDPI_TEST_ZAPRET")
	if src == "" {
		t.Fatal("FDPI_TEST_ZAPRET (deps/linux/zapret) not set")
	}
	base := "/run/f-dpi-test"
	_ = os.RemoveAll(base)
	zapret := filepath.Join(base, "zapret")
	if err := os.MkdirAll(filepath.Join(zapret, "files"), 0o755); err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(base)
	rootTestCopy(t, filepath.Join(src, "nfqws"), filepath.Join(zapret, "nfqws"), 0o755)
	rootTestCopy(t, filepath.Join(src, "files", "quic_initial_www_google_com.bin"), filepath.Join(zapret, "files", "quic_initial_www_google_com.bin"), 0o644)
	zapretDirForTest = zapret
	defer func() { zapretDirForTest = "" }()

	site := os.Getenv("FDPI_TEST_SITE")
	if site == "" {
		site = "discord.com"
	}
	hostlist := filepath.Join(base, "hostlist.txt")
	if err := os.WriteFile(hostlist, []byte(site+"\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	args := []string{"--filter-tcp=80,443", "--hostlist=" + hostlist, "--dpi-desync=fake", "--dpi-desync-fooling=md5sig",
		"--new", "--filter-udp=443", "--filter-l7=quic", "--hostlist=" + hostlist, "--dpi-desync=fake", "--dpi-desync-repeats=6",
		"--dpi-desync-fake-quic=" + filepath.Join(zapret, "files", "quic_initial_www_google_com.bin")}
	if custom := os.Getenv("FDPI_TEST_STRATEGY"); custom != "" {
		args = strings.Fields(custom)
	}

	t.Logf("before:   %s %s", site, rootTestFetch(site))

	// As a setuid core runs: euid 0, the real uid the user's. sudo or "wsl -u root" give a real uid of 0, for which
	// every file is "the user's own".
	const userUID = 1000
	if err := syscall.Setresuid(userUID, 0, 0); err != nil {
		t.Fatalf("setresuid: %v", err)
	}
	defer syscall.Setresuid(0, 0, 0)
	if err := os.Chown(hostlist, userUID, userUID); err != nil {
		t.Fatal(err)
	}
	userDir := filepath.Join(base, "user")
	if err := os.Mkdir(userDir, 0o755); err != nil {
		t.Fatal(err)
	}
	_ = os.Chown(userDir, userUID, userUID)
	link := filepath.Join(userDir, "list.txt")
	if err := os.Symlink("/etc/shadow", link); err != nil {
		t.Fatal(err)
	}
	_ = os.Lchown(link, userUID, userUID)

	// Refusals first: none of these may start anything.
	for _, bad := range [][]string{{"--qnum=1"}, {"--hostlist=/etc/shadow"}, {"--hostlist=" + link}, {"@/etc/passwd"}} {
		if err := Start(StartRequest{Args: bad, TCPPorts: []string{"443"}}); err == nil {
			Stop()
			t.Errorf("%v was accepted", bad)
		} else {
			t.Logf("refused %v: %v", bad, err)
		}
	}

	if err := Start(StartRequest{Args: args, TCPPorts: []string{"80,443"}, UDPPorts: []string{"443"}, Grace: 1500 * time.Millisecond}); err != nil {
		t.Fatalf("start: %v", err)
	}
	if !rootTestTablePresent(t) {
		t.Error("nftables table missing while running")
	}
	status := Query(0, 0)
	t.Logf("running=%v lines=%d", status.Running, len(status.Lines))
	t.Logf("with DPI: %s %s", site, rootTestFetch(site))
	t.Logf("other:    example.com %s", rootTestFetch("example.com"))

	final := Stop()
	_ = final
	if rootTestTablePresent(t) {
		t.Error("nftables table left after Stop")
	}
	entries, _ := os.ReadDir(runRoot)
	for _, entry := range entries {
		if strings.HasPrefix(entry.Name(), fmt.Sprintf("%d-", os.Getpid())) {
			t.Errorf("runtime folder %s left after Stop", entry.Name())
		}
	}
	for _, line := range Query(0, 0).Lines {
		t.Logf("nfqws: %s", line)
	}
	t.Logf("after:    %s %s", site, rootTestFetch(site))
}
