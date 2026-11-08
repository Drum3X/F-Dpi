#!/usr/bin/env bash
# F-Dpi: local Linux (x86_64) build - mirrors build_windows.ps1.
#
# Requires script/fetch_deps.sh to have been run (and the distro packages it reports).
# Output: deployment/linux-amd64/  (F-Dpi, ThroneCore, Qt libs/plugins, zapret/nfqws + files, sing-box)
#
#   bash script/build_linux.sh
#   bash script/build_linux.sh --skip-core --build-type Debug
#   bash script/build_linux.sh --version 0.2.0 --no-deploy-qt
#
# Options:
#   --build-type T      RelWithDebInfo (default) | Release | Debug
#   --skip-core         reuse the last core build (deployment/.core-linux-amd64)
#   --skip-gui          only the core and the runtime files
#   --with-naive        build the core with naive (needs the Chromium toolchain env CI sets up)
#   --no-deploy-qt      do not copy Qt into the output (runs against deps/linux/toolchain/Qt via RPATH)
#   --clean             wipe build-linux/ and the output first
#   --version V         F-Dpi version (default 0.1.0)
#   --sysroot DIR       extra headers/dev libraries unpacked without root (apt-get download <pkg>; dpkg -x <deb> DIR),
#                       for a machine where the distro's -dev packages cannot be installed
#
# Building from /mnt/c in WSL works but is slow; a WSL-side copy is much faster:
#   rsync -rlt --delete --exclude /deps --exclude '/build*' --exclude /deployment /mnt/c/.../F-DPI/ ~/F-DPI/
set -euo pipefail

BUILD_TYPE=RelWithDebInfo
SKIP_CORE=false
SKIP_GUI=false
WITH_NAIVE=false
DEPLOY_QT=true
CLEAN=false
VERSION=0.1.0
SYSROOT=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-type) BUILD_TYPE="$2"; shift 2 ;;
        --skip-core) SKIP_CORE=true; shift ;;
        --skip-gui) SKIP_GUI=true; shift ;;
        --with-naive) WITH_NAIVE=true; shift ;;
        --no-deploy-qt) DEPLOY_QT=false; shift ;;
        --clean) CLEAN=true; shift ;;
        --version) VERSION="$2"; shift 2 ;;
        --sysroot) SYSROOT="$(cd "$2" && pwd)"; shift 2 ;;
        -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLCHAIN="$ROOT/deps/linux/toolchain"
DEST="$ROOT/deployment/linux-amd64"
BUILD_DIR="$ROOT/build-linux"
CORE_STAGE="$ROOT/deployment/.core-linux-amd64"
QT="$TOOLCHAIN/Qt"

step() { printf '\033[36m==> %s\033[0m\n' "$*"; }

[[ -x "$TOOLCHAIN/go/bin/go" && -x "$TOOLCHAIN/py/bin/cmake" && -f "$QT/lib/cmake/Qt6/Qt6Config.cmake" ]] \
    || { echo "toolchain missing - run script/fetch_deps.sh" >&2; exit 1; }

export PATH="$TOOLCHAIN/go/bin:$TOOLCHAIN/gobin:$TOOLCHAIN/protoc/bin:$TOOLCHAIN/py/bin:$PATH"
export GOROOT="$TOOLCHAIN/go"
export CMAKE_PREFIX_PATH="$QT/lib/cmake"
# cmake/nkr.cmake reads it at configure time.
export INPUT_VERSION="$VERSION"

if $CLEAN; then step "clean"; rm -rf "$DEST" "$BUILD_DIR" "$CORE_STAGE"; fi
mkdir -p "$DEST" "$BUILD_DIR"

# ---------------------------------------------------------------- Go core
# build_go.sh wipes $DEST, so the core is built into a staging dir and copied (also on --skip-core).
if ! $SKIP_CORE; then
    step "core (script/build_go.sh)"
    no_naive=1; $WITH_NAIVE && no_naive=0
    (cd "$ROOT" && GOOS=linux GOARCH=amd64 DEST="$CORE_STAGE" NO_NAIVE=$no_naive bash ./script/build_go.sh)
fi
if [[ -x "$CORE_STAGE/ThroneCore" ]]; then cp -f "$CORE_STAGE"/* "$DEST/"
else echo "warning: core not built yet - run without --skip-core" >&2; fi

# ---------------------------------------------------------------- Qt GUI
if ! $SKIP_GUI; then
    [[ -f "$BUILD_DIR/srslist.h" ]] || { echo "build-linux/srslist.h missing - run script/fetch_deps.sh" >&2; exit 1; }
    step "gui configure ($BUILD_TYPE)"
    extra=()
    if [[ -n "$SYSROOT" ]]; then
        multiarch="$SYSROOT/usr/lib/x86_64-linux-gnu"
        extra=(-DCMAKE_INCLUDE_PATH="$SYSROOT/usr/include" -DCMAKE_LIBRARY_PATH="$multiarch"
               -DCMAKE_CXX_FLAGS="-isystem $SYSROOT/usr/include" -DCMAKE_C_FLAGS="-isystem $SYSROOT/usr/include")
        export PKG_CONFIG_PATH="$multiarch/pkgconfig:$SYSROOT/usr/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
        export PATH="$SYSROOT/usr/bin:$PATH"
    fi
    cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++" "${extra[@]}"
    step "gui build"
    cmake --build "$BUILD_DIR"
    install -m 755 "$BUILD_DIR/F-Dpi" "$DEST/F-Dpi"
    if [[ "$BUILD_TYPE" != Debug ]]; then
        # As deploy_linux64.sh: debug info beside the binary (package_linux.sh leaves it out).
        objcopy --only-keep-debug "$DEST/F-Dpi" "$DEST/F-Dpi.debug"
        strip --strip-debug --strip-unneeded "$DEST/F-Dpi"
        objcopy --add-gnu-debuglink="$DEST/F-Dpi.debug" "$DEST/F-Dpi"
    fi
fi

# ---------------------------------------------------------------- Qt runtime
# The same layout deploy_linux64.sh produces in CI: usr/lib + usr/plugins, RPATH $ORIGIN/usr/lib
# (Bootstrap.cpp adds <appdir>/usr/plugins to the plugin path).
if ! $SKIP_GUI && $DEPLOY_QT; then
    step "qt runtime -> usr/lib, usr/plugins"
    command -v patchelf >/dev/null || { echo "patchelf missing (sudo apt install patchelf), or pass --no-deploy-qt" >&2; exit 1; }
    rm -rf "$DEST/usr"; mkdir -p "$DEST/usr/lib" "$DEST/usr/plugins"
    # Every Qt library the binary and the copied plugins need, resolved against the toolchain.
    plugins=(platforms/libqxcb.so platforms/libqwayland.so platformthemes imageformats iconengines
             wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client tls xcbglintegrations)
    for p in "${plugins[@]}"; do
        [[ -e "$QT/plugins/$p" ]] || continue
        mkdir -p "$DEST/usr/plugins/$(dirname "$p")"
        cp -r "$QT/plugins/$p" "$DEST/usr/plugins/$(dirname "$p")/"
    done
    collect() {
        LD_LIBRARY_PATH="$QT/lib" ldd "$1" 2>/dev/null | awk '/=> \//{print $3}' | grep -E "^$QT/lib/" || true
    }
    mapfile -t libs < <({ collect "$DEST/F-Dpi"; find "$DEST/usr/plugins" -name '*.so' -print0 | while IFS= read -r -d '' so; do collect "$so"; done; } | sort -u)
    for lib in "${libs[@]}"; do cp -L "$lib" "$DEST/usr/lib/"; done
    # ICU comes with Qt's prebuilt; ldd lists it through libQt6Core.
    patchelf --set-rpath '$ORIGIN/usr/lib' "$DEST/F-Dpi"
    find "$DEST/usr/plugins" -name '*.so' -exec patchelf --set-rpath '$ORIGIN/../../lib' {} \;
elif ! $SKIP_GUI; then
    command -v patchelf >/dev/null && patchelf --set-rpath "$QT/lib" "$DEST/F-Dpi" || true
fi

# ---------------------------------------------------------------- F-Dpi runtime binaries
step "runtime: zapret + sing-box"
if [[ -x "$ROOT/deps/linux/zapret/nfqws" ]]; then
    # A privileged core runs zapret/nfqws only when root owns it; "give the core root privileges" chowns the folder,
    # after which it is left alone while unchanged.
    if [[ -d "$DEST/zapret" ]] && diff -rq "$ROOT/deps/linux/zapret" "$DEST/zapret" >/dev/null 2>&1; then
        :
    elif [[ -d "$DEST/zapret" && ! -w "$DEST/zapret" ]]; then
        echo "warning: $DEST/zapret is root's and out of date; remove it (sudo rm -rf) and build again" >&2
    else
        mkdir -p "$DEST/zapret"
        cp -a "$ROOT/deps/linux/zapret/." "$DEST/zapret/"
    fi
else echo "warning: deps/linux/zapret missing - run script/fetch_deps.sh" >&2; fi
if [[ -x "$ROOT/deps/linux/sing-box/sing-box" ]]; then cp -f "$ROOT/deps/linux/sing-box/sing-box" "$DEST/"
else echo "warning: deps/linux/sing-box missing" >&2; fi
cp -f "$ROOT/res/fdpi/F-Dpi.png" "$DEST/" 2>/dev/null || true

step "done -> $DEST"
