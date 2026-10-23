<#
.SYNOPSIS
    F-Dpi: local Windows (x64) build - mirrors the CI windows-amd64 legs.

.DESCRIPTION
    Requires script\fetch_deps.ps1 to have been run and Visual Studio (Build Tools) with the C++ workload.
    Output: deployment\windows-amd64\  (GUI exe, core, cronet, zapret runtime, sing-box)

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File script\build_windows.ps1
    powershell -ExecutionPolicy Bypass -File script\build_windows.ps1 -SkipCore -BuildType Debug
    powershell -ExecutionPolicy Bypass -File script\build_windows.ps1 -SkipCore -UpdateTranslations
#>
[CmdletBinding()]
param(
    [ValidateSet("RelWithDebInfo", "Release", "Debug")]
    [string]$BuildType = "RelWithDebInfo",
    [switch]$SkipCore,
    [switch]$SkipGui,
    [switch]$UpdateTranslations,   # refresh res/translations/*.ts from the sources before building
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Toolchain = Join-Path $Root "deps\toolchain"
$Dest = Join-Path $Root "deployment\windows-amd64"
$BuildDir = Join-Path $Root "build"

function Write-Step([string]$msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Invoke-Checked([scriptblock]$cmd, [string]$what) {
    & $cmd
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)" }
}

# ---------------------------------------------------------------- environment
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "Visual Studio Build Tools not found (vswhere.exe missing)" }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "No Visual Studio instance with the C++ x64 toolset" }

Write-Step "MSVC environment from $vsPath"
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
# vcvars looks vswhere up on PATH itself.
$env:PATH = "$(Split-Path $vswhere);$env:PATH"
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match "^([^=]+)=(.*)$") { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
}

$env:PATH = @(
    "$Toolchain\go\bin",
    "$Toolchain\gobin",
    "$Toolchain\protoc\bin",
    "$Toolchain\cmake\bin",
    "$Toolchain\ninja",
    $env:PATH
) -join ";"
$env:GOROOT = "$Toolchain\go"
$env:CMAKE_PREFIX_PATH = "$Toolchain\Qt\lib\cmake"
$env:OPENSSL_ROOT_DIR = "$Toolchain\openssl"

# Git Bash only: the bash.exe stubs in System32/WindowsApps start WSL, which drops the env vars build_go.sh reads.
$bash = "$env:ProgramFiles\Git\bin\bash.exe"
if (-not (Test-Path $bash)) {
    $git = (Get-Command git.exe -ErrorAction SilentlyContinue).Source
    if ($git) { $bash = Join-Path (Split-Path (Split-Path $git)) "bin\bash.exe" }
}
if (-not (Test-Path $bash)) { throw "Git Bash not found (install Git for Windows)" }

if ($Clean) {
    Write-Step "clean"
    Remove-Item -Recurse -Force $Dest, $BuildDir -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force $Dest, $BuildDir | Out-Null

# ---------------------------------------------------------------- Go core
# build_go.sh wipes $DEST, so the core is built into a staging dir and copied (also on -SkipCore).
$coreStage = Join-Path $Root "deployment\.core-windows-amd64"
if (-not $SkipCore) {
    Write-Step "core (script/build_go.sh)"
    $env:GOOS = "windows"; $env:GOARCH = "amd64"; $env:DEST = ($coreStage -replace "\\", "/")
    Push-Location $Root
    try { Invoke-Checked { & $bash ./script/build_go.sh } "core build" } finally { Pop-Location }
    Remove-Item Env:\GOOS, Env:\GOARCH, Env:\DEST
}
if (Test-Path "$coreStage\ThroneCore.exe") { Copy-Item -Force "$coreStage\*" $Dest }
else { Write-Warning "core not built yet - run without -SkipCore" }

# ---------------------------------------------------------------- Qt GUI
if (-not $SkipGui) {
    if (-not (Test-Path "$BuildDir\srslist.h")) { throw "build\srslist.h missing - run script\fetch_deps.ps1" }
    Write-Step "gui configure ($BuildType)"
    Invoke-Checked { cmake -S $Root -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$BuildType" } "cmake configure"
    if ($UpdateTranslations) {
        Write-Step "lupdate (res/translations/*.ts)"
        Invoke-Checked { cmake --build $BuildDir --target update_translations } "update_translations"
    }
    Write-Step "gui build"
    Invoke-Checked { cmake --build $BuildDir } "ninja build"
    Get-ChildItem "$BuildDir\*.exe", "$BuildDir\*.pdb" -ErrorAction SilentlyContinue | Copy-Item -Destination $Dest -Force
}

# ---------------------------------------------------------------- F-Dpi runtime binaries
Write-Step "runtime: zapret + sing-box"
$zapretSrc = Join-Path $Root "deps\zapret\zapret-winws"
if (Test-Path $zapretSrc) {
    $zapretDst = Join-Path $Dest "zapret"
    New-Item -ItemType Directory -Force $zapretDst | Out-Null
    Copy-Item -Recurse -Force "$zapretSrc\*" $zapretDst
} else { Write-Warning "deps\zapret missing - run script\fetch_deps.ps1" }
$singbox = Join-Path $Root "deps\sing-box\sing-box.exe"
if (Test-Path $singbox) { Copy-Item -Force $singbox $Dest } else { Write-Warning "deps\sing-box missing" }

Write-Step "done -> $Dest"
