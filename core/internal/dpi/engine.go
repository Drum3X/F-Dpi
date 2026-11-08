package dpi

import (
	"sync"
	"time"
)

const (
	// QueueNum is the NFQUEUE number nfqws reads; zapret's own scripts use 200 too, so they cannot run beside F-Dpi.
	QueueNum = 200
	// DesyncMark marks the packets nfqws itself sends; the queue rule skips them (zapret's default mark).
	DesyncMark = 0x40000000
	// nfqws drops to nobody once its sockets are open.
	dropUID = 65534
	dropGID = 65534
	// Packets of a connection, both directions, that go through nfqws: enough for the handshake and the first request.
	queuePackets = 12
	maxLines     = 400
)

// StartRequest starts nfqws.
type StartRequest struct {
	Args     []string
	TCPPorts []string
	UDPPorts []string
	Grace    time.Duration
}

// Status is what Query reports.
type Status struct {
	Running  bool
	Exited   bool // it ran and has exited since the last Start
	ExitCode int
	Lines    []string
	LastSeq  int64
	// The learned autohostlist, when it changed after the version the caller has (0: never read).
	AutoHostlist        []byte
	AutoHostlistVersion int64
	AutoHostlistChanged bool
}

type lineRing struct {
	mu    sync.Mutex
	lines []string
	seq   int64 // seq of the last line
}

func (r *lineRing) add(line string) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.lines = append(r.lines, line)
	if len(r.lines) > maxLines {
		r.lines = r.lines[len(r.lines)-maxLines:]
	}
	r.seq++
}

func (r *lineRing) since(after int64) ([]string, int64) {
	r.mu.Lock()
	defer r.mu.Unlock()
	n := r.seq - after
	if n <= 0 {
		return nil, r.seq
	}
	if n > int64(len(r.lines)) {
		n = int64(len(r.lines))
	}
	return append([]string(nil), r.lines[len(r.lines)-int(n):]...), r.seq
}

func (r *lineRing) last() string {
	r.mu.Lock()
	defer r.mu.Unlock()
	for i := len(r.lines) - 1; i >= 0; i-- {
		if r.lines[i] != "" {
			return r.lines[i]
		}
	}
	return ""
}
