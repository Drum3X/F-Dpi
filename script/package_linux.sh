#!/usr/bin/env bash
# F-Dpi: .deb and .rpm packages from deployment/linux-amd64 (script/build_linux.sh).
#
#   bash script/package_linux.sh --version 0.1.0            # both
#   bash script/package_linux.sh --version 0.1.0 --deb       # dpkg-deb only
#   bash script/package_linux.sh --version 0.1.0 --rpm       # rpmbuild only
#
# Output: deployment/F-Dpi-<version>-linux-amd64.deb / .rpm
#
# Layout: /opt/F-Dpi (F-Dpi, ThroneCore setuid root, zapret/ root's), /usr/share/applications/F-Dpi.desktop,
# /usr/share/icons/hicolor/512x512/apps/f-dpi.png. The core is setuid root in the package itself: it runs nfqws, the
# nftables queue rule and the systemd-resolved changes for the GUI, which runs as the user (data: ~/.config/F-Dpi).
# Throne's own pack_debian.sh / pack_rpm.sh (CI) are left as they are.
set -euo pipefail

VERSION=0.1.0
DEB=false
RPM=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) VERSION="${2#v}"; shift 2 ;;
        --deb) DEB=true; shift ;;
        --rpm) RPM=true; shift ;;
        -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
if ! $DEB && ! $RPM; then DEB=true; RPM=true; fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/deployment/linux-amd64"
OUT="$ROOT/deployment"
[[ -x "$SRC/F-Dpi" && -x "$SRC/ThroneCore" && -x "$SRC/zapret/nfqws" ]] || { echo "$SRC is incomplete - run script/build_linux.sh" >&2; exit 1; }

step() { printf '\033[36m==> %s\033[0m\n' "$*"; }

# usr/lib bundles Qt and ICU only; these come from the system (the same list as Throne's .deb).
DEB_DEPENDS="desktop-file-utils, libegl1, libgl1, libopengl0, libglx0, libfontconfig1, libfreetype6, libxkbcommon0, libx11-6, libx11-xcb1, libxcb1, libxcb-cursor0, libdbus-1-3, libglib2.0-0 | libglib2.0-0t64, libssl3 | libssl3t64, ca-certificates"
DEB_RECOMMENDS="systemd-resolved | systemd, network-manager"
RPM_REQUIRES="desktop-file-utils, mesa-libEGL, mesa-libGL, libglvnd-opengl, fontconfig, freetype, libxkbcommon, libX11, libX11-xcb, libxcb, xcb-util-cursor, dbus-libs, glib2, openssl-libs, ca-certificates"

GLIBC_NEED=$( (find "$SRC" -type f \( -name F-Dpi -o -name ThroneCore -o -name nfqws -o -name '*.so*' \) -exec readelf -V -W {} + 2>/dev/null || true) \
    | { grep -o 'GLIBC_[0-9][0-9.]*' || true; } | sed 's/^GLIBC_//' | sort -Vu | tail -n1)
[[ -n "$GLIBC_NEED" ]] && DEB_DEPENDS="libc6 (>= $GLIBC_NEED), $DEB_DEPENDS"

# One staged tree for both formats.
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
chmod 0755 "$STAGE"
mkdir -p "$STAGE/opt" "$STAGE/usr/share/applications" "$STAGE/usr/share/icons/hicolor/512x512/apps"
cp -a "$SRC" "$STAGE/opt/F-Dpi"
rm -rf "$STAGE/opt/F-Dpi/config" "$STAGE/opt/F-Dpi/F-Dpi.debug" "$STAGE/opt/F-Dpi/updater"
find "$STAGE/opt/F-Dpi" -type d -exec chmod 0755 {} +
find "$STAGE/opt/F-Dpi" -type f -exec chmod go-w {} +
chmod 4755 "$STAGE/opt/F-Dpi/ThroneCore"
install -m 0644 "$ROOT/res/fdpi/Running.png" "$STAGE/usr/share/icons/hicolor/512x512/apps/f-dpi.png"
cat >"$STAGE/usr/share/applications/F-Dpi.desktop" <<-EOF
[Desktop Entry]
Name=F-Dpi
Comment=Get around censorship and DPI blocking (sing-box proxy, zapret DPI bypass)
Comment[tr]=İnternet sansürünü ve DPI engellerini aşın (sing-box proxy, zapret DPI bypass)
Exec=/opt/F-Dpi/F-Dpi -appdata
Icon=f-dpi
Terminal=false
Type=Application
Categories=Network;
EOF

# Stops a running F-Dpi before its files go: a clean exit puts the DNS back. Its core and nfqws go with it (nfqws dies with
# the core, the nftables table with the core's socket).
STOP_SCRIPT='pids=$(pidof /opt/F-Dpi/F-Dpi 2>/dev/null || true)
if [ -n "$pids" ]; then
    kill -TERM $pids 2>/dev/null || true
    i=0
    while [ $i -lt 50 ] && pidof /opt/F-Dpi/F-Dpi >/dev/null 2>&1; do sleep 0.1; i=$((i + 1)); done
    pids=$(pidof /opt/F-Dpi/F-Dpi 2>/dev/null || true)
    [ -n "$pids" ] && kill -KILL $pids 2>/dev/null || true
fi'

if $DEB; then
    step "deb $VERSION"
    DEB_ROOT=$(mktemp -d)
    trap 'rm -rf "$STAGE" "$DEB_ROOT"' EXIT
    chmod 0755 "$DEB_ROOT"
    cp -a "$STAGE/." "$DEB_ROOT/"
    mkdir -p "$DEB_ROOT/DEBIAN"
    cat >"$DEB_ROOT/DEBIAN/control" <<-EOF
	Package: f-dpi
	Version: $VERSION
	Architecture: amd64
	Maintainer: F-Dpi
	Depends: $DEB_DEPENDS
	Recommends: $DEB_RECOMMENDS
	Section: net
	Priority: optional
	Description: Get around censorship and DPI blocking
	 A sing-box proxy (Throne based) and a zapret (nfqws) DPI bypass that never run at the same time,
	 with per-network configs, Blockcheck and automatic encrypted DNS.
	EOF
    printf '#!/bin/sh\nupdate-desktop-database >/dev/null 2>&1 || true\ngtk-update-icon-cache -q /usr/share/icons/hicolor >/dev/null 2>&1 || true\nexit 0\n' >"$DEB_ROOT/DEBIAN/postinst"
    printf '#!/bin/sh\n%s\nexit 0\n' "$STOP_SCRIPT" >"$DEB_ROOT/DEBIAN/prerm"
    printf '#!/bin/sh\nupdate-desktop-database >/dev/null 2>&1 || true\nexit 0\n' >"$DEB_ROOT/DEBIAN/postrm"
    chmod 0755 "$DEB_ROOT/DEBIAN/postinst" "$DEB_ROOT/DEBIAN/prerm" "$DEB_ROOT/DEBIAN/postrm"
    dpkg-deb --root-owner-group -Zxz --build "$DEB_ROOT" "$OUT/F-Dpi-$VERSION-linux-amd64.deb"
fi

if $RPM; then
    command -v rpmbuild >/dev/null || { echo "rpmbuild missing (sudo apt install rpm / dnf install rpm-build)" >&2; exit 1; }
    step "rpm $VERSION"
    WORK=$(mktemp -d)
    trap 'rm -rf "$STAGE" "${DEB_ROOT:-}" "$WORK"' EXIT
    mkdir -p "$WORK"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS}
    # rpm versions take no '-'.
    RPM_VERSION="${VERSION//-/_}"
    cat >"$WORK/f-dpi.spec" <<-EOF
	Name: f-dpi
	Version: ${RPM_VERSION}
	Release: 1
	Summary: Get around censorship and DPI blocking
	License: GPL-3.0-or-later
	AutoReqProv: no
	%define debug_package %{nil}
	%define __os_install_post %{nil}
	Requires: ${RPM_REQUIRES}
	Recommends: systemd-resolved, NetworkManager

	%description
	A sing-box proxy (Throne based) and a zapret (nfqws) DPI bypass that never run at the same time,
	with per-network configs, Blockcheck and automatic encrypted DNS.

	%install
	rm -rf %{buildroot}
	mkdir -p %{buildroot}
	cp -a ${STAGE}/. %{buildroot}/

	%files
	%defattr(-,root,root,-)
	/opt/F-Dpi
	/usr/share/applications/F-Dpi.desktop
	/usr/share/icons/hicolor/512x512/apps/f-dpi.png

	%post
	update-desktop-database >/dev/null 2>&1 || :

	%preun
	${STOP_SCRIPT}

	%postun
	update-desktop-database >/dev/null 2>&1 || :
	EOF
    rpmbuild -bb --define "_topdir $WORK" --target x86_64 "$WORK/f-dpi.spec"
    mv "$WORK/RPMS/x86_64/f-dpi-${RPM_VERSION}-1.x86_64.rpm" "$OUT/F-Dpi-$VERSION-linux-amd64.rpm"
fi

step "done -> $OUT"
