<#
.SYNOPSIS
    F-Dpi: downloads the build toolchain and the runtime binaries into deps/.

.DESCRIPTION
    Idempotent: a component whose marker file already exists is skipped unless -Force is given.
    Everything lands under deps/ (git-ignored); nothing needs administrator rights.

      deps/toolchain/go        Go toolchain (core build)
      deps/toolchain/cmake     CMake
      deps/toolchain/ninja     Ninja
      deps/toolchain/protoc    protoc (+ protoc-gen-go / protoc-gen-go-grpc in deps/toolchain/gobin)
      deps/toolchain/Qt        Qt (throneproj/buildqt prebuilt, same as CI)
      deps/toolchain/openssl   OpenSSL (throneproj/env_windows_legacy, same as CI)
      deps/sing-box            official sing-box release (standalone, optional runtime/fallback)
      deps/zapret              bol-van/zapret-win-bundle (winws.exe, WinDivert, lists, blockcheck)

    Also fetches res/dashboard (sing-box-dashboard gh-pages) and build/srslist.h, like CI does.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File script\fetch_deps.ps1
    powershell -ExecutionPolicy Bypass -File script\fetch_deps.ps1 -Only zapret,singbox -Force
#>
[CmdletBinding()]
param(
    [string[]]$Only = @(),
    [switch]$Force,
    [string]$QtVersion = "6.12.0",
    [string]$GoVersion = "",          # empty = latest stable from go.dev
    [string]$ProtocVersion = "31.1",
    [string]$ProtocGenGoVersion = "v1.36.12",
    [string]$ProtocGenGoGrpcVersion = "v1.6.2",
    [string]$SingBoxVersion = "",     # empty = latest GitHub release
    [string]$ZapretRef = "master"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Root = Split-Path -Parent $PSScriptRoot
$Deps = Join-Path $Root "deps"
$Toolchain = Join-Path $Deps "toolchain"
$Cache = Join-Path $Deps ".cache"
$Log = Join-Path $Deps "fetch_deps.log"
New-Item -ItemType Directory -Force $Toolchain, $Cache | Out-Null

function Write-Step([string]$msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

function Wants([string]$name) { return ($Only.Count -eq 0) -or ($Only -contains $name) }

function Get-Json([string]$url) {
    $headers = @{ "User-Agent" = "F-Dpi-fetch-deps" }
    if ($env:GITHUB_TOKEN) { $headers["Authorization"] = "Bearer $env:GITHUB_TOKEN" }
    return Invoke-RestMethod -Uri $url -Headers $headers
}

function Get-File([string]$url, [string]$name) {
    $out = Join-Path $Cache $name
    if ((Test-Path $out) -and -not $Force) { return $out }
    Write-Host "    download $url"
    Invoke-WebRequest -Uri $url -OutFile "$out.part" -UseBasicParsing -Headers @{ "User-Agent" = "F-Dpi-fetch-deps" }
    Move-Item -Force "$out.part" $out
    $hash = (Get-FileHash -Algorithm SHA256 $out).Hash
    Add-Content -Path $Log -Value ("{0}  {1}  {2}  {3}" -f (Get-Date -Format s), $hash, $name, $url)
    return $out
}

# Extracts into a fresh, empty directory, then replaces $dest with it.
function Expand-Into([string]$archive, [string]$dest) {
    $tmp = "$dest.extract"
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    New-Item -ItemType Directory -Force $tmp | Out-Null
    if ($archive -like "*.zip") {
        Expand-Archive -Path $archive -DestinationPath $tmp -Force
    } else {
        # Windows' bundled bsdtar (libarchive) reads .7z.
        & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $tmp
        if ($LASTEXITCODE -ne 0) { throw "tar failed on $archive" }
    }
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Move-Item $tmp $dest
}

# Collapses a single top-level directory produced by most release archives.
function Get-SingleRoot([string]$dir) {
    $items = @(Get-ChildItem -Force $dir)
    if ($items.Count -eq 1 -and $items[0].PSIsContainer) { return $items[0].FullName }
    return $dir
}

function Install-Flattened([string]$archive, [string]$dest) {
    Expand-Into $archive "$dest.raw"
    $inner = Get-SingleRoot "$dest.raw"
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Move-Item $inner $dest
    if (Test-Path "$dest.raw") { Remove-Item -Recurse -Force "$dest.raw" }
}

function Get-LatestRelease([string]$repo) { return Get-Json "https://api.github.com/repos/$repo/releases/latest" }

function Skip([string]$marker) { return (Test-Path $marker) -and -not $Force }

# ---------------------------------------------------------------- toolchain
if (Wants "go") {
    $dest = Join-Path $Toolchain "go"
    if (Skip "$dest\bin\go.exe") { Write-Step "go: present" } else {
        $ver = $GoVersion
        if (-not $ver) {
            # PS 5.1 pipes a JSON array as one object; assigning first enumerates it.
            $releases = Get-Json "https://go.dev/dl/?mode=json"
            $ver = ($releases | Where-Object { $_.stable } | Select-Object -First 1).version
        }
        if ($ver -notlike "go*") { $ver = "go$ver" }
        Write-Step "go: $ver"
        $zip = Get-File "https://go.dev/dl/$ver.windows-amd64.zip" "$ver.windows-amd64.zip"
        Install-Flattened $zip $dest
    }
}

if (Wants "cmake") {
    $dest = Join-Path $Toolchain "cmake"
    if (Skip "$dest\bin\cmake.exe") { Write-Step "cmake: present" } else {
        $rel = Get-LatestRelease "Kitware/CMake"
        $asset = $rel.assets | Where-Object { $_.name -match "windows-x86_64\.zip$" } | Select-Object -First 1
        Write-Step "cmake: $($rel.tag_name)"
        Install-Flattened (Get-File $asset.browser_download_url $asset.name) $dest
    }
}

if (Wants "ninja") {
    $dest = Join-Path $Toolchain "ninja"
    if (Skip "$dest\ninja.exe") { Write-Step "ninja: present" } else {
        $rel = Get-LatestRelease "ninja-build/ninja"
        $asset = $rel.assets | Where-Object { $_.name -eq "ninja-win.zip" } | Select-Object -First 1
        Write-Step "ninja: $($rel.tag_name)"
        Expand-Into (Get-File $asset.browser_download_url "ninja-$($rel.tag_name)-win.zip") $dest
    }
}

if (Wants "protoc") {
    $dest = Join-Path $Toolchain "protoc"
    if (Skip "$dest\bin\protoc.exe") { Write-Step "protoc: present" } else {
        Write-Step "protoc: $ProtocVersion"
        $name = "protoc-$ProtocVersion-win64.zip"
        Expand-Into (Get-File "https://github.com/protocolbuffers/protobuf/releases/download/v$ProtocVersion/$name" $name) $dest
    }
    $gobin = Join-Path $Toolchain "gobin"
    $go = Join-Path $Toolchain "go\bin\go.exe"
    if ((Test-Path $go) -and -not (Skip "$gobin\protoc-gen-go-grpc.exe")) {
        Write-Step "protoc-gen-go $ProtocGenGoVersion / protoc-gen-go-grpc $ProtocGenGoGrpcVersion"
        $env:GOBIN = $gobin
        & $go install "google.golang.org/protobuf/cmd/protoc-gen-go@$ProtocGenGoVersion"
        & $go install "google.golang.org/grpc/cmd/protoc-gen-go-grpc@$ProtocGenGoGrpcVersion"
        Remove-Item Env:\GOBIN
    }
}

if (Wants "qt") {
    $dest = Join-Path $Toolchain "Qt"
    if (Skip "$dest\lib\cmake\Qt6\Qt6Config.cmake") { Write-Step "qt: present" } else {
        Write-Step "qt: $QtVersion (throneproj/buildqt)"
        $name = "Qt_${QtVersion}_x64.7z"
        Install-Flattened (Get-File "https://github.com/throneproj/buildqt/releases/download/Qt_$QtVersion/$name" $name) $dest
    }
}

if (Wants "openssl") {
    $dest = Join-Path $Toolchain "openssl"
    if (Skip "$dest\include\openssl\ssl.h") { Write-Step "openssl: present" } else {
        Write-Step "openssl (throneproj/env_windows_legacy)"
        Install-Flattened (Get-File "https://github.com/throneproj/env_windows_legacy/releases/download/latest/openssl_x64.7z" "openssl_x64.7z") $dest
    }
}

# ---------------------------------------------------------------- runtime binaries
if (Wants "singbox") {
    $dest = Join-Path $Deps "sing-box"
    if (Skip "$dest\sing-box.exe") { Write-Step "sing-box: present" } else {
        $rel = if ($SingBoxVersion) { Get-Json "https://api.github.com/repos/SagerNet/sing-box/releases/tags/v$($SingBoxVersion.TrimStart('v'))" } else { Get-LatestRelease "SagerNet/sing-box" }
        $asset = $rel.assets | Where-Object { $_.name -match "windows-amd64\.zip$" -and $_.name -notmatch "legacy" } | Select-Object -First 1
        Write-Step "sing-box: $($rel.tag_name)"
        Install-Flattened (Get-File $asset.browser_download_url $asset.name) $dest
        Set-Content -Path "$dest\VERSION" -Value $rel.tag_name
    }
}

if (Wants "zapret") {
    $dest = Join-Path $Deps "zapret"
    if (Skip "$dest\zapret-winws\winws.exe") { Write-Step "zapret-win-bundle: present" } else {
        $commit = (Get-Json "https://api.github.com/repos/bol-van/zapret-win-bundle/commits/$ZapretRef").sha
        Write-Step "zapret-win-bundle: $ZapretRef @ $($commit.Substring(0, 12))"
        $zip = Get-File "https://github.com/bol-van/zapret-win-bundle/archive/$commit.zip" "zapret-win-bundle-$commit.zip"
        Install-Flattened $zip $dest
        Set-Content -Path "$dest\VERSION" -Value $commit
    }
}

# ---------------------------------------------------------------- build inputs CI also fetches
if (Wants "dashboard") {
    $dest = Join-Path $Root "res\dashboard"
    if (Skip "$dest\index.html") { Write-Step "dashboard: present" } else {
        Write-Step "sing-box-dashboard (gh-pages)"
        $zip = Get-File "https://github.com/SagerNet/sing-box-dashboard/archive/refs/heads/gh-pages.zip" "sing-box-dashboard-gh-pages.zip"
        Expand-Into $zip "$Cache\dashboard"
        $inner = Get-SingleRoot "$Cache\dashboard"
        New-Item -ItemType Directory -Force $dest | Out-Null
        Copy-Item -Recurse -Force "$inner\*" $dest
        Remove-Item -Recurse -Force "$Cache\dashboard"
    }
}

if (Wants "srslist") {
    $build = Join-Path $Root "build"
    New-Item -ItemType Directory -Force $build | Out-Null
    if (Skip "$build\srslist.h") { Write-Step "srslist.h: present" } else {
        Write-Step "srslist.h (throneproj/routeprofiles)"
        Invoke-WebRequest -UseBasicParsing -Uri "https://raw.githubusercontent.com/throneproj/routeprofiles/rule-set/srslist.h" -OutFile "$build\srslist.h"
    }
}

Write-Step "done (hashes logged to deps\fetch_deps.log)"
