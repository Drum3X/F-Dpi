# F-Dpi for Nix. The GUI runs as the user; the core must be setuid root for DPI Bypass, Tun and the system DNS, which a
# store path cannot be: use the NixOS module (nix/module.nix, security.wrappers). Layout: $out/opt/F-Dpi holds F-Dpi,
# ThroneCore and zapret/ side by side (the core's parent check and its nfqws trust check both need that), $out/bin/f-dpi
# is the Qt wrapper script.
#
# Every download the regular build fetches is pinned here (script/fetch_deps.sh fetches the same versions).
{
  lib,
  stdenv,
  buildGoModule,
  fetchurl,
  unzip,
  cmake,
  ninja,
  pkg-config,
  qt6,
  protobuf,
  protoc-gen-go,
  protoc-gen-go-grpc,
  libnetfilter_queue,
  libnfnetlink,
  libmnl,
  zlib,
  libcap,
  libxcb,
  src,
  version ? "0.1.0",
}:
let
  zapretVersion = "72.13";
  zapretSrc = fetchurl {
    url = "https://github.com/bol-van/zapret/releases/download/v${zapretVersion}/zapret-v${zapretVersion}.tar.gz";
    sha256 = "25c74e6c5f48963fa244c2955e76694a07c39447245a0457e2efdc74b3317e68";
  };
  listYoutube = fetchurl {
    url = "https://raw.githubusercontent.com/bol-van/zapret-win-bundle/de9795d71287fdb21cf8c20b8af1a09d0eb10ab2/zapret-winws/files/list-youtube.txt";
    sha256 = "47ea8a9ca11eb9ad833e5f22d12e8857400880e8f555f45687230ce8ba57d299";
  };
  srslist = fetchurl {
    url = "https://raw.githubusercontent.com/throneproj/routeprofiles/a9cd4d7e7e3e52f1cad8ff202c442ff2b40ae1e6/srslist.h";
    sha256 = "52c60a622d911587caf95b136046dec561a8e07ad3f955d65562d666c98abe67";
  };
  dashboard = fetchurl {
    url = "https://github.com/SagerNet/sing-box-dashboard/archive/d77c2f016a7681aa1704e9f83c69139edb230c97.zip";
    sha256 = "fa3afee26f13fb5a980a8df7598d996c2dba2d5bf9aa4e1ce0b63c46c757511f";
  };

  # nfqws from source (the release tarball's prebuilt binary is static, but a store build should not depend on it).
  nfqws = stdenv.mkDerivation {
    pname = "zapret-nfqws";
    version = zapretVersion;
    src = zapretSrc;
    nativeBuildInputs = [ pkg-config ];
    buildInputs = [ libnetfilter_queue libnfnetlink libmnl zlib libcap ];
    buildPhase = ''
      runHook preBuild
      make -C nfq CC=${stdenv.cc.targetPrefix}cc
      runHook postBuild
    '';
    installPhase = ''
      runHook preInstall
      install -Dm755 nfq/nfqws $out/bin/nfqws
      mkdir -p $out/share/zapret/files
      cp files/fake/*.bin $out/share/zapret/files/
      install -Dm644 docs/LICENSE.txt $out/share/zapret/LICENSE.txt || true
      runHook postInstall
    '';
  };

  # script/build_go.sh's tags, without naive (it needs the Chromium toolchain CI downloads).
  coreTags = [
    "with_clash_api" "with_quic" "with_wireguard" "with_utls" "with_dhcp" "with_tailscale" "with_openvpn" "with_openconnect"
    "badlinkname" "tfogo_checklinkname0"
  ];
  generateProto = ''
    (cd gen && protoc -I . --go_out=. --go-grpc_out=. libcore.proto)
  '';
  core = buildGoModule {
    pname = "f-dpi-core";
    inherit version src;
    modRoot = "core";
    vendorHash = "sha256-s9+ZnZ2jSb+MB3XluKYoAm4TjIS8j+V+A0vyc5fI4eQ=";
    nativeBuildInputs = [ protobuf protoc-gen-go protoc-gen-go-grpc ];
    # gen/*.pb.go are generated, not committed; vendoring needs them as well.
    overrideModAttrs = old: {
      nativeBuildInputs = (old.nativeBuildInputs or [ ]) ++ [ protobuf protoc-gen-go protoc-gen-go-grpc ];
      preBuild = generateProto;
    };
    preBuild = generateProto;
    env.CGO_ENABLED = "1";
    tags = coreTags;
    ldflags = [ "-w" "-s" "-checklinkname=0" "-X internal/godebug.defaultGODEBUG=multipathtcp=0" ];
    subPackages = [ "." ];
    doCheck = false;
  };
in
stdenv.mkDerivation {
  pname = "f-dpi";
  inherit version src;

  nativeBuildInputs = [ cmake ninja pkg-config unzip qt6.wrapQtAppsHook qt6.qttools ];
  buildInputs = [ qt6.qtbase qt6.qtsvg qt6.qtwayland libxcb ];

  # The wrapper must not replace opt/F-Dpi/F-Dpi: the core checks that its parent is exactly that file.
  dontWrapQtApps = true;

  env.INPUT_VERSION = version;
  cmakeFlags = [
    "-DNKR_PACKAGE=ON"
    "-DNKR_CORE_IN_PATH=ON"
    "-DNKR_DESKTOP_EXEC=f-dpi"
    "-DNKR_ELEVATION_HINT=Enable programs.f-dpi in your NixOS configuration: it installs the setuid core (security.wrappers)."
  ];

  preConfigure = ''
    mkdir -p res/dashboard build
    unzip -q ${dashboard} -d dashboard-src
    cp -r dashboard-src/*/. res/dashboard/
    cp ${srslist} build/srslist.h
  '';

  installPhase = ''
    runHook preInstall
    dir=$out/opt/F-Dpi
    mkdir -p $dir/zapret/files $out/bin $out/share/applications $out/share/icons/hicolor/512x512/apps
    install -m755 F-Dpi $dir/F-Dpi
    install -m755 ${core}/bin/ThroneCore $dir/ThroneCore
    install -m755 ${nfqws}/bin/nfqws $dir/zapret/nfqws
    cp ${nfqws}/share/zapret/files/* $dir/zapret/files/
    cp ${listYoutube} $dir/zapret/files/list-youtube.txt
    makeQtWrapper $dir/F-Dpi $out/bin/f-dpi --add-flags -appdata
    install -m644 $src/res/fdpi/Running.png $out/share/icons/hicolor/512x512/apps/f-dpi.png
    cat >$out/share/applications/F-Dpi.desktop <<EOF
    [Desktop Entry]
    Name=F-Dpi
    Comment=Get around censorship and DPI blocking (sing-box proxy, zapret DPI bypass)
    Comment[tr]=İnternet sansürünü ve DPI engellerini aşın (sing-box proxy, zapret DPI bypass)
    Exec=f-dpi
    Icon=f-dpi
    Terminal=false
    Type=Application
    Categories=Network;
    EOF
    sed -i 's/^    //' $out/share/applications/F-Dpi.desktop
    runHook postInstall
  '';

  passthru = { inherit core nfqws; };

  meta = {
    description = "Get around censorship and DPI blocking: sing-box proxy and zapret DPI bypass";
    license = lib.licenses.gpl3Plus;
    platforms = [ "x86_64-linux" ];
    mainProgram = "f-dpi";
  };
}
