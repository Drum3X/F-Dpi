<p align="center">
  <img src="res/fdpi/Running.png" width="96" alt="F-Dpi">
</p>

<h1 align="center">F-Dpi</h1>

<p align="center">
  <b>English</b> · <a href="README.tr.md">Türkçe</a>
</p>

F-Dpi is a Windows desktop app for getting around internet censorship and DPI (deep packet inspection) blocks. It puts
two engines behind one window:

- **Proxy** through [sing-box](https://github.com/SagerNet/sing-box), with everything [Throne](https://github.com/throneproj/Throne)
  offers (VLESS, Trojan, Shadowsocks, Hysteria, WireGuard, subscriptions, routing, TUN mode, ...).
- **DPI Bypass** through [zapret](https://github.com/bol-van/zapret)'s `winws`, which works without any server: it reshapes
  your own traffic so the censor's filter no longer recognizes it.

The two never run at the same time: starting one stops the other.

F-Dpi is built on Throne and keeps its interface.

## Features

- **DPI Bypass profiles:** ready-made strategies or your own `winws` options, edited in the profile editor.
- **Blockcheck:** finds the strategy that works on your network.
  - *Quick scan* tries the common strategies and stops at the first that opens every blocked site.
  - *Deep scan* tries every combination.
  - The result is saved as a profile, or applied to the one you are editing.
- **Split bypass:** apply the bypass only to blocked sites (a list, or learned automatically), or keep sites out of it.
  Ready lists for Discord and YouTube are included, and one for game launchers to leave alone.
- **General and per-network configs:** profile groups are either *General* or bound to a Wi-Fi/Ethernet network.
  - F-Dpi runs the config of the network you connect to, else the general one.
  - Profiles switch on their own when the network changes.
- **Network placeholders:** write `%GATEWAY%`, `%LOCAL_IP%`, `%DHCP_DNS%`, `%INTERFACE%` or `%GATEWAY6%` in a profile and
  F-Dpi fills in the current network's value when the profile starts.
  - This suits a phone hotspot whose address changes on every connection, e.g. a SOCKS proxy at `%GATEWAY%:10808`.
  - Placeholders only work in profiles saved in this computer's editor, never in imported ones.
- **Automatic DNS (anti DNS poisoning):** F-Dpi measures encrypted DNS providers (Cloudflare, Google, Quad9, AdGuard,
  OpenDNS, Mullvad) and uses the fastest.
  - It checks whether your network's DNS is poisoned.
  - While DPI Bypass runs, it sets Windows itself to DNS-over-HTTPS and restores your settings afterwards.
- **Connect on launch:** F-Dpi starts the right config for the current network when it opens.
- **Safe to remove:** quitting, updating or uninstalling puts back the Windows DNS settings and unloads the WinDivert
  driver.

## Requirements

- Windows 10 1809 or later, 64-bit (Windows 11 recommended: Automatic DNS for DPI Bypass uses its DNS-over-HTTPS).
- Administrator rights. F-Dpi always runs elevated, because WinDivert, TUN mode and the Windows DNS settings need it.

## Installation

Download `F-Dpi-<version>-windows-x64-setup.exe` from [Releases](https://github.com/Drum3X/F-Dpi/releases) and run it.
It installs for all users under `Program Files\F-Dpi`. The installer is available in English and Turkish.

## Quick start

1. **DPI Bypass:** open *Tools → Blockcheck…*, enter a blocked site and run a quick scan. Save the result as a profile and
   start it.
2. **Proxy:** add a profile from a link or subscription (as in Throne) and start it. TUN mode and the system proxy are on
   by default.
3. **Per-network:** switch to *Custom (per network)*, create a group, and use *Add current network* in its *Scope* box.
   That network now gets its own config.
4. **Automatic DNS:** *Tools → Automatic DNS…* shows the chosen server and tests your network for DNS poisoning.

## Building from source

The toolchain is downloaded into `deps/` (no admin rights needed). The only system requirement is Visual Studio 2022
Build Tools with the C++ workload.

```powershell
# Toolchain and runtime binaries (Qt, Go, CMake, Ninja, protoc, sing-box, zapret, Inno Setup)
powershell -ExecutionPolicy Bypass -File script\fetch_deps.ps1
# Go core + Qt GUI -> deployment\windows-amd64\
powershell -ExecutionPolicy Bypass -File script\build_windows.ps1
# With an installer -> deployment\F-Dpi-<version>-windows-x64-setup.exe
powershell -ExecutionPolicy Bypass -File script\build_windows.ps1 -Version 0.1.0 -Installer
```

Developer notes (architecture, conventions, how things were tested) are in [CLAUDE.md](CLAUDE.md), in Turkish.

## Credits and license

F-Dpi is free software under the [GNU GPL v3](LICENSE). It is built on:

- [Throne](https://github.com/throneproj/Throne) (GPL-3.0): the application it is based on.
- [sing-box](https://github.com/SagerNet/sing-box): the proxy core.
- [zapret](https://github.com/bol-van/zapret) and [zapret-win-bundle](https://github.com/bol-van/zapret-win-bundle):
  `winws` and its lists.
- [WinDivert](https://github.com/basil00/WinDivert): packet capture on Windows.

## Disclaimer

F-Dpi is meant for reaching information that is blocked where you are. You are responsible for following the laws of
your country. The software comes without any warranty.
