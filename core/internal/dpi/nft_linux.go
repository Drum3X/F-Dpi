//go:build linux

package dpi

import (
	"encoding/binary"
	"errors"
	"fmt"
	"log"
	"os"
	"slices"

	"github.com/sagernet/nftables"
	"github.com/sagernet/nftables/binaryutil"
	"github.com/sagernet/nftables/expr"
	"golang.org/x/sys/unix"
)

const tableFlagOwner = 0x2 // NFT_TABLE_F_OWNER

// nftRules is an owner table: the kernel deletes it when the core's socket closes, so a dead core leaves no queue
// rule behind (and the queue's bypass flag lets traffic through while nfqws is not reading).
type nftRules struct {
	conn  *nftables.Conn
	table *nftables.Table
}

// installRules queues the first packets of each TCP/UDP connection to the given ports, as zapret's own firewall
// rules do. Incoming adds the server's first packets, which autohostlist and autottl read.
//
//	postrouting 101: meta l4proto P oifname != lo P dport PORTS meta mark & MARK == 0 ct packets 1-12 queue num 200 bypass
//	prerouting -150: meta l4proto P iifname != lo P sport PORTS ct packets 1-12 queue num 200 bypass      (incoming)
//	output -300:     meta mark & MARK != 0 notrack       (nfqws' fakes, often with a TTL that never arrives)
//
// sagernet/nftables cannot set a direction on "ct packets", so both directions count (zapret: original 1-6).
func installRules(tcp, udp [][2]uint16, incoming bool) (*nftRules, error) {
	r := &nftRules{table: &nftables.Table{Name: fmt.Sprintf("fdpi-%d", os.Getpid()), Family: nftables.TableFamilyINet, Flags: tableFlagOwner}}
	var err error
	if r.conn, err = nftables.New(nftables.AsLasting()); err != nil {
		return nil, nftError("open the nftables socket", err)
	}
	if _, err = r.conn.ListTablesOfFamily(nftables.TableFamilyINet); err != nil {
		_ = r.conn.CloseLasting()
		return nil, nftError("list the nftables tables", err)
	}
	accept := nftables.ChainPolicyAccept
	chain := func(name string, hook *nftables.ChainHook, prio nftables.ChainPriority) *nftables.Chain {
		return r.conn.AddChain(&nftables.Chain{Name: name, Table: r.table, Type: nftables.ChainTypeFilter, Hooknum: hook, Priority: nftables.ChainPriorityRef(prio), Policy: &accept})
	}
	r.conn.CreateTable(r.table)
	post := chain("post", nftables.ChainHookPostrouting, 101)
	out := chain("out", nftables.ChainHookOutput, -300)
	var pre *nftables.Chain
	if incoming {
		pre = chain("pre", nftables.ChainHookPrerouting, -150)
	}
	add := func(c *nftables.Chain, exprs ...[]expr.Any) {
		r.conn.AddRule(&nftables.Rule{Table: r.table, Chain: c, Exprs: slices.Concat(exprs...)})
	}
	queue := []expr.Any{&expr.Queue{Num: QueueNum, Flag: expr.QueueFlagBypass}}
	for _, proto := range []struct {
		num   byte
		ports [][2]uint16
	}{{unix.IPPROTO_TCP, tcp}, {unix.IPPROTO_UDP, udp}} {
		for _, port := range proto.ports {
			l4 := matchMeta(expr.MetaKeyL4PROTO, []byte{proto.num})
			add(post, l4, notIfname(expr.MetaKeyOIFNAME, "lo"), portRange(2, port), markClear(), firstPackets(), queue)
			if pre != nil {
				add(pre, l4, notIfname(expr.MetaKeyIIFNAME, "lo"), portRange(0, port), firstPackets(), queue)
			}
		}
	}
	add(out, markSet(), []expr.Any{&expr.Notrack{}})

	if err := r.conn.Flush(); err != nil {
		_ = r.conn.CloseLasting()
		return nil, installError(err)
	}
	table, err := r.conn.ListTableOfFamily(r.table.Name, nftables.TableFamilyINet)
	if err != nil || table.Flags&tableFlagOwner == 0 {
		r.remove()
		return nil, errors.New("DPI Bypass needs Linux 5.12 or newer (nftables table ownership)")
	}
	log.Printf("dpi: nftables table inet %s installed (tcp %v, udp %v, incoming %v)", r.table.Name, tcp, udp, incoming)
	return r, nil
}

func (r *nftRules) remove() {
	if r == nil || r.conn == nil {
		return
	}
	r.conn.DelTable(r.table)
	if err := r.conn.Flush(); err != nil && !errors.Is(err, unix.ENOENT) {
		log.Printf("dpi: delete nftables table %s: %v", r.table.Name, err)
	}
	_ = r.conn.CloseLasting()
	r.conn = nil
}

func installError(err error) error {
	switch {
	case errnoIn(err, unix.EPERM, unix.EACCES):
		return fmt.Errorf("installing the nftables rules was refused (root privileges needed): %w", err)
	case errnoIn(err, unix.ENOENT, unix.EOPNOTSUPP, unix.EINVAL):
		return fmt.Errorf("the kernel lacks nftables queue support (nft_queue, nfnetlink_queue): %w", err)
	}
	return fmt.Errorf("install the nftables rules: %w", err)
}

func nftError(op string, err error) error {
	switch {
	case errnoIn(err, unix.EPERM, unix.EACCES):
		return fmt.Errorf("%s: root privileges needed: %w", op, err)
	case errnoIn(err, unix.EPROTONOSUPPORT, unix.ENOENT, unix.EINVAL, unix.EOPNOTSUPP, unix.EAFNOSUPPORT):
		return errors.New("nftables is not available in this kernel")
	}
	return fmt.Errorf("%s: %w", op, err)
}

func errnoIn(err error, errnos ...unix.Errno) bool {
	return slices.ContainsFunc(errnos, func(errno unix.Errno) bool { return errors.Is(err, errno) })
}

func matchMeta(key expr.MetaKey, data []byte) []expr.Any {
	return []expr.Any{&expr.Meta{Key: key, Register: 1}, &expr.Cmp{Op: expr.CmpOpEq, Register: 1, Data: data}}
}

func notIfname(key expr.MetaKey, name string) []expr.Any {
	data := make([]byte, unix.IFNAMSIZ)
	copy(data, name)
	return []expr.Any{&expr.Meta{Key: key, Register: 1}, &expr.Cmp{Op: expr.CmpOpNeq, Register: 1, Data: data}}
}

// offset 0 = source port, 2 = destination port.
func portRange(offset uint32, port [2]uint16) []expr.Any {
	load := &expr.Payload{OperationType: expr.PayloadLoad, DestRegister: 1, Base: expr.PayloadBaseTransportHeader, Offset: offset, Len: 2}
	if port[0] == port[1] {
		return []expr.Any{load, &expr.Cmp{Op: expr.CmpOpEq, Register: 1, Data: binaryutil.BigEndian.PutUint16(port[0])}}
	}
	return []expr.Any{load, &expr.Range{Op: expr.CmpOpEq, Register: 1,
		FromData: binaryutil.BigEndian.PutUint16(port[0]), ToData: binaryutil.BigEndian.PutUint16(port[1])}}
}

// The mark is host byte order, as nft writes it.
func markBits(op expr.CmpOp) []expr.Any {
	return []expr.Any{
		&expr.Meta{Key: expr.MetaKeyMARK, Register: 1},
		&expr.Bitwise{SourceRegister: 1, DestRegister: 1, Len: 4, Mask: binaryutil.NativeEndian.PutUint32(DesyncMark), Xor: make([]byte, 4)},
		&expr.Cmp{Op: op, Register: 1, Data: make([]byte, 4)},
	}
}

func markClear() []expr.Any { return markBits(expr.CmpOpEq) }
func markSet() []expr.Any   { return markBits(expr.CmpOpNeq) }

// ct packets is a host-order u64; nft converts it to network order before a range, as here.
func firstPackets() []expr.Any {
	from, to := make([]byte, 8), make([]byte, 8)
	binary.BigEndian.PutUint64(from, 1)
	binary.BigEndian.PutUint64(to, queuePackets)
	return []expr.Any{
		&expr.Ct{Key: expr.CtKeyPKTS, Register: 1},
		&expr.Byteorder{SourceRegister: 1, DestRegister: 1, Op: expr.ByteorderHton, Len: 8, Size: 8},
		&expr.Range{Op: expr.CmpOpEq, Register: 1, FromData: from, ToData: to},
	}
}
