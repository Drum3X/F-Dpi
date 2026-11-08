//go:build linux

package fdpidns

import (
	"errors"
	"fmt"
	"net/netip"
	"os"
	"path/filepath"
	"strings"

	"github.com/godbus/dbus/v5"
	"golang.org/x/sys/unix"
)

const (
	resolvedService = "org.freedesktop.resolve1"
	resolvedPath    = dbus.ObjectPath("/org/freedesktop/resolve1")
	managerIface    = "org.freedesktop.resolve1.Manager"
	linkIface       = "org.freedesktop.resolve1.Link"
)

type dnsEx struct {
	Family  int32
	Address []byte
	Port    uint16
	Name    string
}

type dnsPlain struct {
	Family  int32
	Address []byte
}

type domain struct {
	Name      string
	RouteOnly bool
}

// supported: resolved runs and programs resolve through it (resolv.conf points at its stub, or is its file).
func supported() bool {
	conn, err := dbus.ConnectSystemBus()
	if err != nil {
		return false
	}
	defer conn.Close()
	var owned bool
	if err := conn.BusObject().Call("org.freedesktop.DBus.NameHasOwner", 0, resolvedService).Store(&owned); err != nil || !owned {
		return false
	}
	if target, err := filepath.EvalSymlinks("/etc/resolv.conf"); err == nil && strings.HasPrefix(target, "/run/systemd/resolve/") {
		return true
	}
	content, err := os.ReadFile("/etc/resolv.conf")
	if err != nil {
		return false
	}
	return strings.Contains(string(content), "127.0.0.53") || strings.Contains(string(content), "127.0.0.54")
}

func linkObject(conn *dbus.Conn, ifindex int32) (dbus.BusObject, error) {
	var path dbus.ObjectPath
	if err := conn.Object(resolvedService, resolvedPath).Call(managerIface+".GetLink", 0, ifindex).Store(&path); err != nil {
		return nil, fmt.Errorf("link %d: %w", ifindex, err)
	}
	return conn.Object(resolvedService, path), nil
}

func get(links []Link) ([]Link, error) {
	conn, err := dbus.ConnectSystemBus()
	if err != nil {
		return nil, err
	}
	defer conn.Close()
	out := make([]Link, 0, len(links))
	for _, want := range links {
		obj, err := linkObject(conn, want.Ifindex)
		if err != nil {
			return nil, err
		}
		link := Link{Ifindex: want.Ifindex, Servers: []string{}, Domains: []string{}}
		var servers []dnsEx
		if v, err := obj.GetProperty(linkIface + ".DNSEx"); err == nil && v.Store(&servers) == nil {
			for _, s := range servers {
				if addr, ok := netip.AddrFromSlice(s.Address); ok {
					link.Servers = append(link.Servers, formatServer(addr, s.Port, s.Name))
				}
			}
		} else {
			var plain []dnsPlain
			v, err := obj.GetProperty(linkIface + ".DNS")
			if err != nil {
				return nil, fmt.Errorf("link %d DNS: %w", want.Ifindex, err)
			}
			if err := v.Store(&plain); err != nil {
				return nil, err
			}
			for _, s := range plain {
				if addr, ok := netip.AddrFromSlice(s.Address); ok {
					link.Servers = append(link.Servers, formatServer(addr, 0, ""))
				}
			}
		}
		if v, err := obj.GetProperty(linkIface + ".DNSOverTLS"); err == nil {
			_ = v.Store(&link.DNSOverTLS)
		}
		var domains []domain
		if v, err := obj.GetProperty(linkIface + ".Domains"); err == nil && v.Store(&domains) == nil {
			for _, d := range domains {
				if d.RouteOnly {
					link.Domains = append(link.Domains, "~"+d.Name)
				} else {
					link.Domains = append(link.Domains, d.Name)
				}
			}
		}
		if v, err := obj.GetProperty(linkIface + ".DefaultRoute"); err == nil {
			var route bool
			if v.Store(&route) == nil {
				link.DefaultRoute = &route
			}
		}
		out = append(out, link)
	}
	return out, nil
}

func set(links []Link) error {
	if os.Geteuid() != 0 {
		return errors.New("the core has no root privileges")
	}
	conn, err := dbus.ConnectSystemBus()
	if err != nil {
		return err
	}
	defer conn.Close()
	manager := conn.Object(resolvedService, resolvedPath)
	var errs []error
	for _, link := range links {
		servers := make([]dnsEx, 0, len(link.Servers))
		for _, text := range link.Servers {
			addr, port, name, err := parseServer(text)
			if err != nil {
				return err
			}
			family := int32(unix.AF_INET)
			if addr.Is6() {
				family = unix.AF_INET6
			}
			servers = append(servers, dnsEx{Family: family, Address: addr.AsSlice(), Port: port, Name: name})
		}
		if call := manager.Call(managerIface+".SetLinkDNSEx", 0, link.Ifindex, servers); call.Err != nil {
			// resolved < 246 has no DNSEx: no port or name, so no DNS-over-TLS name check either.
			plain := make([]dnsPlain, 0, len(servers))
			for _, s := range servers {
				plain = append(plain, dnsPlain{Family: s.Family, Address: s.Address})
			}
			if call := manager.Call(managerIface+".SetLinkDNS", 0, link.Ifindex, plain); call.Err != nil {
				errs = append(errs, fmt.Errorf("link %d DNS: %w", link.Ifindex, call.Err))
				continue
			}
		}
		if call := manager.Call(managerIface+".SetLinkDNSOverTLS", 0, link.Ifindex, link.DNSOverTLS); call.Err != nil {
			errs = append(errs, fmt.Errorf("link %d DNSOverTLS: %w", link.Ifindex, call.Err))
		}
		domains := make([]domain, 0, len(link.Domains))
		for _, d := range link.Domains {
			domains = append(domains, domain{Name: strings.TrimPrefix(d, "~"), RouteOnly: strings.HasPrefix(d, "~")})
		}
		if call := manager.Call(managerIface+".SetLinkDomains", 0, link.Ifindex, domains); call.Err != nil {
			errs = append(errs, fmt.Errorf("link %d domains: %w", link.Ifindex, call.Err))
		}
		if link.DefaultRoute != nil {
			if call := manager.Call(managerIface+".SetLinkDefaultRoute", 0, link.Ifindex, *link.DefaultRoute); call.Err != nil {
				errs = append(errs, fmt.Errorf("link %d default route: %w", link.Ifindex, call.Err))
			}
		}
	}
	return errors.Join(errs...)
}

func flush() error {
	conn, err := dbus.ConnectSystemBus()
	if err != nil {
		return err
	}
	defer conn.Close()
	return conn.Object(resolvedService, resolvedPath).Call(managerIface+".FlushCaches", 0).Err
}

func formatServer(addr netip.Addr, port uint16, name string) string {
	text := addr.String()
	if port != 0 && port != 53 {
		text = netip.AddrPortFrom(addr, port).String()
	}
	if name != "" {
		text += "#" + name
	}
	return text
}

func parseServer(text string) (netip.Addr, uint16, string, error) {
	host, name, _ := strings.Cut(text, "#")
	if addr, err := netip.ParseAddr(host); err == nil {
		return addr.Unmap(), 0, name, nil
	}
	if ap, err := netip.ParseAddrPort(host); err == nil {
		return ap.Addr().Unmap(), ap.Port(), name, nil
	}
	return netip.Addr{}, 0, "", fmt.Errorf("%q is not a DNS server address", text)
}
