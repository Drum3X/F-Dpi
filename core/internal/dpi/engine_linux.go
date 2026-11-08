//go:build linux

package dpi

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"golang.org/x/sys/unix"
)

// Root-owned runtime directory: the copies of the files nfqws reads, one subdirectory per start.
const runRoot = "/run/f-dpi-core"

const maxFileSize = 32 << 20

// The root test's zapret folder (engine_root_linux_test.go); the trust rules still apply to it.
var zapretDirForTest string

type instance struct {
	cmd      *exec.Cmd
	done     chan struct{}
	exitCode int
	stopping atomic.Bool
	rules    *nftRules
	runDir   string
	autoPath string   // the runtime copy of the autohostlist, "" without one
	guard    *os.File // write end of the guard pipe; closing it stops nfqws
}

var (
	mu         sync.Mutex
	current    *instance
	generation int
	ring       = &lineRing{}
	// The last instance's exit, kept for Query until the next Start.
	exited   atomic.Bool
	exitCode atomic.Int64
)

func Start(req StartRequest) error {
	mu.Lock()
	defer mu.Unlock()
	stopLocked()
	exited.Store(false)

	if os.Geteuid() != 0 {
		return errors.New("the core has no root privileges; DPI Bypass needs them on Linux (give the core root privileges)")
	}
	nfqws, zapretDir, err := trustedNfqws()
	if err != nil {
		return err
	}
	parsed, err := Parse(req.Args)
	if err != nil {
		return err
	}
	tcp, err := ParsePorts(req.TCPPorts)
	if err != nil {
		return fmt.Errorf("TCP ports: %w", err)
	}
	udp, err := ParsePorts(req.UDPPorts)
	if err != nil {
		return fmt.Errorf("UDP ports: %w", err)
	}
	if len(tcp) == 0 && len(udp) == 0 {
		return errors.New("no TCP or UDP port to send through nfqws")
	}

	generation++
	runDir, err := makeRunDir(generation)
	if err != nil {
		return err
	}
	inst := &instance{done: make(chan struct{}), runDir: runDir}
	fail := func(err error) error {
		if inst.rules != nil {
			inst.rules.remove()
		}
		_ = os.RemoveAll(runDir)
		return err
	}

	args, err := stageFiles(parsed, runDir, zapretDir, inst)
	if err != nil {
		return fail(err)
	}
	if inst.rules, err = installRules(tcp, udp, parsed.Incoming); err != nil {
		return fail(err)
	}

	full := append([]string{
		"--qnum=" + strconv.Itoa(QueueNum),
		fmt.Sprintf("--dpi-desync-fwmark=0x%x", DesyncMark),
		fmt.Sprintf("--uid=%d:%d", dropUID, dropGID),
	}, args...)
	if err := spawn(inst, nfqws, full); err != nil {
		return fail(fmt.Errorf("nfqws could not be started: %w", err))
	}

	grace := req.Grace
	if grace <= 0 {
		grace = 1500 * time.Millisecond
	}
	select {
	case <-inst.done:
		last := ring.last()
		if last == "" {
			last = "no output"
		}
		return fail(fmt.Errorf("nfqws exited (code %d): %s", inst.exitCode, last))
	case <-time.After(grace):
	}
	current = inst
	go watch(inst)
	return nil
}

// spawn runs nfqws under the guard (RunGuard): nfqws drops to nobody, and the kernel clears a parent-death signal on
// every credential change, so Pdeathsig cannot tie it to the core. The guard stays root and kills nfqws once the
// guard pipe's write end, which only the core holds, closes: on Stop and whenever the core dies, SIGKILL included.
func spawn(inst *instance, nfqws string, args []string) error {
	self, err := os.Executable()
	if err != nil {
		return err
	}
	out, outW, err := os.Pipe()
	if err != nil {
		return err
	}
	guardR, guardW, err := os.Pipe()
	if err != nil {
		_ = out.Close()
		_ = outW.Close()
		return err
	}
	cmd := exec.Command(self, append([]string{guardArg, nfqws}, args...)...)
	cmd.Dir = inst.runDir
	// A setuid Go binary passes its environment on unfiltered; nfqws needs none of it.
	cmd.Env = []string{"PATH=/usr/sbin:/usr/bin:/sbin:/bin"}
	cmd.Stdout, cmd.Stderr = outW, outW
	cmd.ExtraFiles = []*os.File{guardR} // fd 3
	// Its own process group: Stop's last resort kills the guard and nfqws together.
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	err = cmd.Start()
	_ = outW.Close()
	_ = guardR.Close()
	if err != nil {
		_ = out.Close()
		_ = guardW.Close()
		return err
	}
	inst.cmd = cmd
	inst.guard = guardW
	log.Printf("dpi: nfqws started (guard pid %d): %s", cmd.Process.Pid, strings.Join(args, " "))
	go func() {
		readLines(out)
		_ = cmd.Wait()
		inst.exitCode = cmd.ProcessState.ExitCode()
		close(inst.done)
	}()
	return nil
}

const guardArg = "--fdpi-nfqws"

// RunGuard is main()'s "--fdpi-nfqws <nfqws> <args...>": the core's own child that runs nfqws and kills it when the
// pipe on fd 3 closes. Only the core may start it, and only with the nfqws next to it.
func RunGuard() {
	fail := func(format string, a ...any) {
		fmt.Fprintf(os.Stderr, "nfqws guard: "+format+"\n", a...)
		os.Exit(2)
	}
	self, err := os.Executable()
	if err == nil {
		self, err = filepath.EvalSymlinks(self)
	}
	if err != nil {
		fail("%v", err)
	}
	parent, err := os.Readlink(fmt.Sprintf("/proc/%d/exe", os.Getppid()))
	if err != nil || strings.TrimSuffix(parent, " (deleted)") != self {
		fail("not started by the core")
	}
	if len(os.Args) < 3 {
		fail("no nfqws")
	}
	nfqws, _, err := trustedNfqws()
	if err != nil {
		fail("%v", err)
	}
	if os.Args[2] != nfqws {
		fail("%s is not the core's nfqws", os.Args[2])
	}
	guard := os.NewFile(3, "guard")
	if guard == nil {
		fail("no guard pipe")
	}
	cmd := exec.Command(nfqws, os.Args[3:]...)
	cmd.Env = []string{"PATH=/usr/sbin:/usr/bin:/sbin:/bin"}
	cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
	if err := cmd.Start(); err != nil {
		fail("%v", err)
	}
	go func() {
		_, _ = io.Copy(io.Discard, guard) // EOF: Stop, or the core is gone
		_ = cmd.Process.Signal(syscall.SIGTERM)
		time.Sleep(2 * time.Second)
		_ = cmd.Process.Kill()
	}()
	_ = cmd.Wait()
	os.Exit(cmd.ProcessState.ExitCode())
}

func readLines(r io.ReadCloser) {
	defer r.Close()
	scanner := bufio.NewScanner(r)
	scanner.Buffer(make([]byte, 64<<10), 1<<20)
	for scanner.Scan() {
		ring.add(strings.TrimRight(scanner.Text(), "\r"))
	}
}

// An exit the GUI did not ask for: the queue rule goes (bypass would let packets through anyway), the files stay
// for Query until the next Start or Stop.
func watch(inst *instance) {
	<-inst.done
	if inst.stopping.Load() {
		return
	}
	mu.Lock()
	defer mu.Unlock()
	if current != inst {
		return
	}
	log.Printf("dpi: nfqws exited unexpectedly (code %d)", inst.exitCode)
	inst.rules.remove()
	inst.rules = nil
	exitCode.Store(int64(inst.exitCode))
	exited.Store(true)
}

// Stop ends nfqws and returns the final status, the learned autohostlist included.
func Stop() Status {
	mu.Lock()
	defer mu.Unlock()
	return stopLocked()
}

func stopLocked() Status {
	inst := current
	if inst == nil {
		return Status{}
	}
	current = nil
	inst.stopping.Store(true)
	if inst.guard != nil {
		_ = inst.guard.Close()
	}
	if inst.cmd != nil && inst.cmd.Process != nil {
		select {
		case <-inst.done:
		case <-time.After(4 * time.Second):
			// The guard and nfqws share a process group.
			_ = syscall.Kill(-inst.cmd.Process.Pid, syscall.SIGKILL)
			<-inst.done
		}
	}
	status := Status{}
	if inst.autoPath != "" {
		if data, version, err := readAuto(inst.autoPath); err == nil {
			status.AutoHostlist, status.AutoHostlistVersion, status.AutoHostlistChanged = data, version, true
		}
	}
	if inst.rules != nil {
		inst.rules.remove()
	}
	_ = os.RemoveAll(inst.runDir)
	log.Printf("dpi: nfqws stopped")
	return status
}

func Query(afterSeq, autoVersion int64) Status {
	mu.Lock()
	inst := current
	mu.Unlock()
	status := Status{}
	status.Lines, status.LastSeq = ring.since(afterSeq)
	if inst != nil {
		select {
		case <-inst.done:
		default:
			status.Running = true
		}
		if inst.autoPath != "" {
			if fi, err := os.Stat(inst.autoPath); err == nil {
				if version := autoVersionOf(fi); version != autoVersion {
					if data, version, err := readAuto(inst.autoPath); err == nil {
						status.AutoHostlist, status.AutoHostlistVersion, status.AutoHostlistChanged = data, version, true
					}
				}
			}
		}
	}
	status.Exited = exited.Load()
	status.ExitCode = int(exitCode.Load())
	return status
}

func autoVersionOf(fi os.FileInfo) int64 {
	return fi.ModTime().UnixNano() ^ (fi.Size() << 40)
}

func readAuto(path string) ([]byte, int64, error) {
	fi, err := os.Stat(path)
	if err != nil {
		return nil, 0, err
	}
	if fi.Size() > maxFileSize {
		return nil, 0, errors.New("autohostlist too large")
	}
	data, err := os.ReadFile(path)
	return data, autoVersionOf(fi), err
}

// ---------------------------------------------------------------- files

// stageFiles copies every file nfqws would read into runDir and points the options at the copies.
func stageFiles(parsed *Parsed, runDir, zapretDir string, inst *instance) ([]string, error) {
	args := append([]string(nil), parsed.Args...)
	copies := map[string]string{}
	for i, file := range parsed.Files {
		auto := false
		for _, a := range parsed.AutoHostlist {
			auto = auto || a == i
		}
		dst, ok := copies[file.Path]
		if !ok {
			data, err := readUserFile(file.Path, zapretDir)
			if errors.Is(err, os.ErrNotExist) && auto {
				data, err = nil, nil
			}
			if err != nil {
				return nil, fmt.Errorf("--%s: %w", file.Option, err)
			}
			dst = filepath.Join(runDir, fmt.Sprintf("%02d-%s", i, safeName(filepath.Base(file.Path))))
			if err := os.WriteFile(dst, data, 0o644); err != nil {
				return nil, err
			}
			copies[file.Path] = dst
		}
		if auto {
			inst.autoPath = dst
		}
		args[file.Index] = "--" + file.Option + "=" + dst
	}
	return args, nil
}

func safeName(name string) string {
	return strings.Map(func(r rune) rune {
		if r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' || r >= '0' && r <= '9' || r == '.' || r == '-' || r == '_' {
			return r
		}
		return '_'
	}, name)
}

// readUserFile reads a file the invoking user could read anyway: one of theirs, or root's inside the zapret folder
// the core runs nfqws from. No symlink anywhere on the path, so a link cannot lead root elsewhere.
func readUserFile(path, zapretDir string) ([]byte, error) {
	fd, err := unix.Openat2(unix.AT_FDCWD, path, &unix.OpenHow{
		Flags:   unix.O_RDONLY | unix.O_CLOEXEC | unix.O_NOCTTY | unix.O_NONBLOCK,
		Resolve: unix.RESOLVE_NO_SYMLINKS | unix.RESOLVE_NO_MAGICLINKS,
	})
	if err != nil {
		if errors.Is(err, unix.ENOENT) {
			return nil, fmt.Errorf("%s: %w", path, os.ErrNotExist)
		}
		if errors.Is(err, unix.ELOOP) {
			return nil, fmt.Errorf("%s: the path goes through a symbolic link", path)
		}
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	f := os.NewFile(uintptr(fd), path)
	defer f.Close()
	var st unix.Stat_t
	if err := unix.Fstat(fd, &st); err != nil {
		return nil, err
	}
	if st.Mode&unix.S_IFMT != unix.S_IFREG {
		return nil, fmt.Errorf("%s is not a regular file", path)
	}
	if st.Size > maxFileSize {
		return nil, fmt.Errorf("%s is larger than 32 MB", path)
	}
	ownedByUser := st.Uid == uint32(os.Getuid())
	inZapret := st.Uid == 0 && strings.HasPrefix(filepath.Clean(path), zapretDir+string(os.PathSeparator))
	if !ownedByUser && !inZapret {
		return nil, fmt.Errorf("%s belongs to another user", path)
	}
	return io.ReadAll(io.LimitReader(f, maxFileSize))
}

func makeRunDir(gen int) (string, error) {
	if err := os.MkdirAll(runRoot, 0o711); err != nil {
		return "", err
	}
	var st unix.Stat_t
	if err := unix.Lstat(runRoot, &st); err != nil || st.Mode&unix.S_IFMT != unix.S_IFDIR || st.Uid != 0 || st.Mode&0o022 != 0 {
		return "", fmt.Errorf("%s is not root's own directory", runRoot)
	}
	dir := filepath.Join(runRoot, fmt.Sprintf("%d-%d", os.Getpid(), gen))
	_ = os.RemoveAll(dir)
	if err := os.Mkdir(dir, 0o711); err != nil {
		return "", err
	}
	return dir, nil
}

// ---------------------------------------------------------------- nfqws

// trustedNfqws returns <core dir>/zapret/nfqws when no other user can have replaced it: root owns it and its folder,
// and every directory above is root's or the invoking user's and writable by nobody else (a sticky root directory,
// such as /nix/store, counts as root's).
func trustedNfqws() (nfqws, zapretDir string, err error) {
	if zapretDirForTest != "" {
		zapretDir = zapretDirForTest
	} else {
		self, err := os.Executable()
		if err != nil {
			return "", "", err
		}
		if self, err = filepath.EvalSymlinks(self); err != nil {
			return "", "", err
		}
		zapretDir = filepath.Join(filepath.Dir(self), "zapret")
	}
	nfqws = filepath.Join(zapretDir, "nfqws")
	var st unix.Stat_t
	if err := unix.Lstat(nfqws, &st); err != nil {
		return "", "", fmt.Errorf("nfqws was not found at %s", nfqws)
	}
	if st.Mode&unix.S_IFMT != unix.S_IFREG || st.Uid != 0 || st.Mode&0o022 != 0 || st.Mode&0o111 == 0 {
		return "", "", fmt.Errorf("%s must be an executable owned by root and writable by root only (give the core root privileges again)", nfqws)
	}
	for dir := zapretDir; ; dir = filepath.Dir(dir) {
		if err := unix.Lstat(dir, &st); err != nil {
			return "", "", err
		}
		owner := st.Uid == 0 || (dir != zapretDir && st.Uid == uint32(os.Getuid()))
		// Group write is the user's own when the group is their private one (umask 002 distributions).
		groupWritable := st.Mode&0o020 != 0 && !(dir != zapretDir && privateGroup(st.Gid))
		writable := (st.Mode&0o002 != 0 || groupWritable) && !(st.Mode&unix.S_ISVTX != 0 && st.Uid == 0)
		if st.Mode&unix.S_IFMT != unix.S_IFDIR || !owner || writable {
			return "", "", fmt.Errorf("another user could replace nfqws: %s can be written by someone other than root and you", dir)
		}
		if dir == "/" {
			break
		}
	}
	return nfqws, zapretDir, nil
}

// privateGroup: gid is the invoking user's primary group, no other account has it as its primary group and it lists no
// members (a user private group, as Debian, Ubuntu and Fedora create).
func privateGroup(gid uint32) bool {
	if gid != uint32(os.Getgid()) {
		return false
	}
	group, err := os.ReadFile("/etc/group")
	if err != nil {
		return false
	}
	found := false
	for line := range strings.Lines(string(group)) {
		fields := strings.Split(strings.TrimSpace(line), ":")
		if len(fields) < 4 || fields[2] != strconv.FormatUint(uint64(gid), 10) {
			continue
		}
		if strings.TrimSpace(fields[3]) != "" {
			return false
		}
		found = true
	}
	if !found {
		return false
	}
	passwd, err := os.ReadFile("/etc/passwd")
	if err != nil {
		return false
	}
	for line := range strings.Lines(string(passwd)) {
		fields := strings.Split(strings.TrimSpace(line), ":")
		if len(fields) >= 4 && fields[3] == strconv.FormatUint(uint64(gid), 10) && fields[2] != strconv.Itoa(os.Getuid()) {
			return false
		}
	}
	return true
}

// CleanupAtBoot kills an nfqws a previous core left behind (its guard kills it when the core dies; a guard killed
// with SIGKILL itself cannot) and removes runtime directories of cores that are gone.
func CleanupAtBoot() {
	if os.Geteuid() != 0 {
		return
	}
	if nfqws, _, err := trustedNfqws(); err == nil {
		entries, _ := os.ReadDir("/proc")
		for _, entry := range entries {
			pid, err := strconv.Atoi(entry.Name())
			if err != nil || pid == os.Getpid() {
				continue
			}
			exe, err := os.Readlink(fmt.Sprintf("/proc/%d/exe", pid))
			if err != nil || strings.TrimSuffix(exe, " (deleted)") != nfqws {
				continue
			}
			log.Printf("dpi: killing a leftover nfqws (pid %d)", pid)
			_ = syscall.Kill(pid, syscall.SIGKILL)
		}
	}
	entries, _ := os.ReadDir(runRoot)
	for _, entry := range entries {
		pidText, _, _ := strings.Cut(entry.Name(), "-")
		pid, err := strconv.Atoi(pidText)
		if err != nil || pid == os.Getpid() || syscall.Kill(pid, 0) == nil {
			continue
		}
		_ = os.RemoveAll(filepath.Join(runRoot, entry.Name()))
	}
}
