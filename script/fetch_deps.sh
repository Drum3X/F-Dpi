#!/usr/bin/env bash
# F-Dpi: downloads the Linux (x86_64) build toolchain and runtime binaries into deps/linux/.
#
# Idempotent: a component whose marker file exists is skipped unless --force is given.
# Nothing needs root; only the distro's development headers do (checked at the end, see check_system).
#
#   deps/linux/toolchain/go        Go toolchain (core build)
#   deps/linux/toolchain/py        Python venv with cmake, ninja and aqtinstall
#   deps/linux/toolchain/protoc    protoc (+ protoc-gen-go / protoc-gen-go-grpc in deps/linux/toolchain/gobin)
#   deps/linux/toolchain/Qt        Qt (aqtinstall, the same prebuilt install-qt-action uses in CI)
#   deps/linux/sing-box            official sing-box release (standalone, optional runtime/fallback)
#   deps/linux/zapret              nfqws + fake packets (bol-van/zapret release) and list-youtube.txt
#
# Also fetches res/dashboard and build-linux/srslist.h, like CI does.
#
#   bash script/fetch_deps.sh
#   bash script/fetch_deps.sh --only zapret,singbox --force
set -euo pipefail

QT_VERSION=6.12.0
GO_VERSION=""                 # empty = latest stable from go.dev
PROTOC_VERSION=31.1
PROTOC_GEN_GO_VERSION=v1.36.12
PROTOC_GEN_GO_GRPC_VERSION=v1.6.2
SINGBOX_VERSION=""            # empty = latest GitHub release
ZAPRET_VERSION=v72.13
ZAPRET_SHA256=25c74e6c5f48963fa244c2955e76694a07c39447245a0457e2efdc74b3317e68
# Same lists as the Windows bundle (zapret-winws/files); pinned so Windows and Linux ship the same file.
ZAPRET_WIN_BUNDLE_REF=de9795d71287fdb21cf8c20b8af1a09d0eb10ab2

ONLY=""
FORCE=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --only) ONLY="$2"; shift 2 ;;
        --force) FORCE=true; shift ;;
        --qt) QT_VERSION="$2"; shift 2 ;;
        --go) GO_VERSION="$2"; shift 2 ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/deps/linux"
TOOLCHAIN="$DEPS/toolchain"
CACHE="$DEPS/.cache"
LOG="$DEPS/fetch_deps.log"
mkdir -p "$TOOLCHAIN" "$CACHE"

step() { printf '\033[36m==> %s\033[0m\n' "$*"; }
wants() { [[ -z "$ONLY" || ",$ONLY," == *",$1,"* ]]; }
skip() { [[ -e "$1" ]] && ! $FORCE; }

curl_gh() {
    local auth=()
    [[ -n "${GITHUB_TOKEN:-}" ]] && auth=(-H "Authorization: Bearer $GITHUB_TOKEN")
    curl -fsSL -H "User-Agent: F-Dpi-fetch-deps" "${auth[@]}" "$@"
}

# get_file <url> <name> -> prints the cached path; logs the SHA256 of each new download.
get_file() {
    local url="$1" out="$CACHE/$2"
    if [[ ! -f "$out" ]] || $FORCE; then
        echo "    download $url" >&2
        curl -fL --retry 3 --no-progress-meter -H "User-Agent: F-Dpi-fetch-deps" -o "$out.part" "$url"
        mv -f "$out.part" "$out"
        printf '%s  %s  %s  %s\n' "$(date -Iseconds)" "$(sha256sum "$out" | cut -d' ' -f1)" "$2" "$url" >> "$LOG"
    fi
    echo "$out"
}

json() { python3 -c "import json,sys; d=json.load(sys.stdin); print(eval(sys.argv[1]))" "$1"; }

# extract <archive> <dest>: unpacks into a fresh directory and collapses a single top-level folder.
extract() {
    local archive="$1" dest="$2" tmp="$2.extract"
    rm -rf "$tmp"; mkdir -p "$tmp"
    case "$archive" in
        *.zip) python3 -m zipfile -e "$archive" "$tmp" ;;
        *) tar -xf "$archive" -C "$tmp" ;;
    esac
    local inner="$tmp" items
    mapfile -t items < <(ls -A "$tmp")
    if [[ ${#items[@]} -eq 1 && -d "$tmp/${items[0]}" ]]; then inner="$tmp/${items[0]}"; fi
    rm -rf "$dest"
    mv "$inner" "$dest"
    rm -rf "$tmp"
}

# ---------------------------------------------------------------- toolchain
if wants go; then
    dest="$TOOLCHAIN/go"
    if skip "$dest/bin/go"; then step "go: present"; else
        ver="$GO_VERSION"
        [[ -z "$ver" ]] && ver=$(curl -fsSL "https://go.dev/dl/?mode=json" | json "[r['version'] for r in d if r['stable']][0]")
        [[ "$ver" == go* ]] || ver="go$ver"
        step "go: $ver"
        extract "$(get_file "https://go.dev/dl/$ver.linux-amd64.tar.gz" "$ver.linux-amd64.tar.gz")" "$dest"
    fi
fi

if wants py; then
    dest="$TOOLCHAIN/py"
    if skip "$dest/bin/aqt"; then step "python tools: present"; else
        step "python venv: cmake, ninja, aqtinstall"
        rm -rf "$dest"
        python3 -m venv "$dest"
        "$dest/bin/pip" install --quiet --upgrade pip
        "$dest/bin/pip" install --quiet cmake ninja aqtinstall
    fi
fi

if wants protoc; then
    dest="$TOOLCHAIN/protoc"
    if skip "$dest/bin/protoc"; then step "protoc: present"; else
        step "protoc: $PROTOC_VERSION"
        name="protoc-$PROTOC_VERSION-linux-x86_64.zip"
        extract "$(get_file "https://github.com/protocolbuffers/protobuf/releases/download/v$PROTOC_VERSION/$name" "$name")" "$dest"
        chmod +x "$dest/bin/protoc"   # zipfile drops the mode bits
    fi
    gobin="$TOOLCHAIN/gobin"
    if [[ -x "$TOOLCHAIN/go/bin/go" ]] && ! skip "$gobin/protoc-gen-go-grpc"; then
        step "protoc-gen-go $PROTOC_GEN_GO_VERSION / protoc-gen-go-grpc $PROTOC_GEN_GO_GRPC_VERSION"
        GOBIN="$gobin" GOROOT="$TOOLCHAIN/go" "$TOOLCHAIN/go/bin/go" install "google.golang.org/protobuf/cmd/protoc-gen-go@$PROTOC_GEN_GO_VERSION"
        GOBIN="$gobin" GOROOT="$TOOLCHAIN/go" "$TOOLCHAIN/go/bin/go" install "google.golang.org/grpc/cmd/protoc-gen-go-grpc@$PROTOC_GEN_GO_GRPC_VERSION"
    fi
fi

if wants qt; then
    dest="$TOOLCHAIN/Qt"
    if skip "$dest/lib/cmake/Qt6/Qt6Config.cmake"; then step "qt: present"; else
        [[ -x "$TOOLCHAIN/py/bin/aqt" ]] || { echo "qt needs the python tools: run without --only, or with --only py first" >&2; exit 1; }
        step "qt: $QT_VERSION (aqtinstall, linux_gcc_64)"
        rm -rf "$CACHE/qt-aqt"
        # The essentials (qtbase, qtsvg, qttools, qttranslations, qtwayland ...) like install-qt-action.
        (cd "$CACHE" && "$TOOLCHAIN/py/bin/aqt" install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$CACHE/qt-aqt")
        rm -rf "$dest"
        mv "$CACHE/qt-aqt/$QT_VERSION/gcc_64" "$dest"
        rm -rf "$CACHE/qt-aqt"
    fi
fi

# ---------------------------------------------------------------- runtime binaries
if wants singbox; then
    dest="$DEPS/sing-box"
    if skip "$dest/sing-box"; then step "sing-box: present"; else
        if [[ -n "$SINGBOX_VERSION" ]]; then rel=$(curl_gh "https://api.github.com/repos/SagerNet/sing-box/releases/tags/v${SINGBOX_VERSION#v}")
        else rel=$(curl_gh "https://api.github.com/repos/SagerNet/sing-box/releases/latest"); fi
        tag=$(echo "$rel" | json "d['tag_name']")
        url=$(echo "$rel" | json "[a['browser_download_url'] for a in d['assets'] if a['name'].endswith('linux-amd64.tar.gz')][0]")
        step "sing-box: $tag"
        extract "$(get_file "$url" "$(basename "$url")")" "$dest"
        echo "$tag" > "$dest/VERSION"
    fi
fi

if wants zapret; then
    dest="$DEPS/zapret"
    if skip "$dest/nfqws"; then step "zapret: present"; else
        step "zapret: $ZAPRET_VERSION (nfqws + fake packets)"
        tarball=$(get_file "https://github.com/bol-van/zapret/releases/download/$ZAPRET_VERSION/zapret-$ZAPRET_VERSION.tar.gz" "zapret-$ZAPRET_VERSION.tar.gz")
        echo "$ZAPRET_SHA256  $tarball" | sha256sum -c --quiet - || { echo "zapret tarball SHA256 mismatch" >&2; exit 1; }
        extract "$tarball" "$CACHE/zapret-src"
        rm -rf "$dest"; mkdir -p "$dest/files"
        install -m 755 "$CACHE/zapret-src/binaries/linux-x86_64/nfqws" "$dest/nfqws"
        # The Windows bundle keeps the fakes directly in files/; presets name them as %ZAPRET%/files/<name>.
        cp "$CACHE/zapret-src/files/fake/"*.bin "$dest/files/"
        cp "$CACHE/zapret-src/docs/LICENSE.txt" "$dest/LICENSE.txt" 2>/dev/null || true
        curl -fsSL -o "$dest/files/list-youtube.txt" \
            "https://raw.githubusercontent.com/bol-van/zapret-win-bundle/$ZAPRET_WIN_BUNDLE_REF/zapret-winws/files/list-youtube.txt"
        printf '%s\nwin-bundle lists %s\n' "$ZAPRET_VERSION" "$ZAPRET_WIN_BUNDLE_REF" > "$dest/VERSION"
        rm -rf "$CACHE/zapret-src"
    fi
fi

# ---------------------------------------------------------------- build inputs CI also fetches
if wants dashboard; then
    dest="$ROOT/res/dashboard"
    if skip "$dest/index.html"; then step "dashboard: present"; else
        step "sing-box-dashboard (gh-pages)"
        extract "$(get_file "https://github.com/SagerNet/sing-box-dashboard/archive/refs/heads/gh-pages.zip" "sing-box-dashboard-gh-pages.zip")" "$CACHE/dashboard"
        mkdir -p "$dest"; cp -r "$CACHE/dashboard/." "$dest/"; rm -rf "$CACHE/dashboard"
    fi
fi

if wants srslist; then
    build="$ROOT/build-linux"
    mkdir -p "$build"
    if skip "$build/srslist.h"; then step "srslist.h: present"; else
        step "srslist.h (throneproj/routeprofiles)"
        curl -fsSL -o "$build/srslist.h" "https://raw.githubusercontent.com/throneproj/routeprofiles/rule-set/srslist.h"
    fi
fi

# ---------------------------------------------------------------- distro packages (need root, so only reported)
check_system() {
    local missing=()
    have_header() { [[ -e "/usr/include/$1" || -e "/usr/include/x86_64-linux-gnu/$1" ]]; }
    have_lib() { ldconfig -p 2>/dev/null | grep -q "$1" || ls /usr/lib/x86_64-linux-gnu/$1* >/dev/null 2>&1; }
    command -v g++ >/dev/null || missing+=(build-essential)
    have_header GL/gl.h || missing+=(libgl-dev)
    have_header EGL/egl.h || missing+=(libegl-dev)
    have_header xcb/xcb.h || missing+=(libxcb1-dev)
    have_header X11/Xlib-xcb.h || missing+=(libx11-xcb-dev)
    have_header xkbcommon/xkbcommon.h || missing+=(libxkbcommon-dev)
    # Runtime of Qt's xcb/wayland platform plugins.
    have_lib libxcb-cursor.so.0 || missing+=(libxcb-cursor0)
    have_lib libxkbcommon-x11.so.0 || missing+=(libxkbcommon-x11-0)
    have_lib libxcb-icccm.so.4 || missing+=(libxcb-icccm4)
    have_lib libxcb-keysyms.so.1 || missing+=(libxcb-keysyms1)
    have_lib libxcb-image.so.0 || missing+=(libxcb-image0)
    have_lib libxcb-render-util.so.0 || missing+=(libxcb-render-util0)
    have_lib libxcb-shape.so.0 || missing+=(libxcb-shape0)
    have_lib libxcb-xinerama.so.0 || missing+=(libxcb-xinerama0)
    have_lib libwayland-cursor.so.0 || missing+=(libwayland-cursor0)
    have_lib libwayland-egl.so.1 || missing+=(libwayland-egl1)
    command -v patchelf >/dev/null || missing+=(patchelf)
    # Runtime of the DPI engine and the privilege flow (tests).
    command -v nft >/dev/null || missing+=(nftables)
    command -v pkexec >/dev/null || missing+=(pkexec)
    if [[ ${#missing[@]} -gt 0 ]]; then
        step "missing distro packages (Debian/Ubuntu names):"
        echo "    sudo apt install ${missing[*]}"
    else
        step "distro packages: ok"
    fi
}
check_system

step "done (hashes logged to deps/linux/fetch_deps.log)"
