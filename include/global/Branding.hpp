#pragma once

// F-Dpi product identity. Only user-visible names and per-product system identifiers live here;
// internal Throne identifiers (throne.db, throneIPC, ThroneCore, :/Throne resources, throne:// links)
// stay unchanged so upstream merges remain small and existing data keeps loading.
namespace Branding {
    // Display name: window titles, message boxes, tray tooltip.
    inline constexpr auto AppName = "F-Dpi";
    // QCoreApplication name: decides the AppConfigLocation data directory, so F-Dpi and Throne never share one.
    inline constexpr auto AppId = "F-Dpi";
    // Single-instance QLocalServer name prefix; distinct from Throne's "throne-" so both can run side by side.
    inline constexpr auto InstancePrefix = "fdpi-";
    // Windows identifiers a Throne install on the same computer must not share (the headless host's presence mapping and
    // window class are F-Dpi's own in HostPresence_win.cpp / ServiceManager_win.cpp):
    // - the registry key the installer records its folder under (script/windows_installer.iss), read by UrlScheme;
    inline constexpr auto InstallRegistryKey = "Software\\F-Dpi";
    // - the ProgId config files open with;
    inline constexpr auto ConfigProgId = "F-Dpi.Config";
    // - the service mode's data folder under ProgramData.
    inline constexpr auto SystemDataDirName = "F-Dpi";
    // Scheduled-task name prefix for autorun.
    inline constexpr auto AutoRunTaskPrefix = "F-Dpi AutoRun ";
    // Project page shown in About; empty hides the button.
    inline constexpr auto HomepageUrl = "";
    // GitHub "owner/repo" whose releases feed the update checker; empty disables update checks.
    inline constexpr auto UpdateRepo = "";
    // Based-on notice for About (GPL-3.0 attribution).
    inline constexpr auto UpstreamName = "Throne";
    inline constexpr auto UpstreamUrl = "https://github.com/throneproj/Throne";
} // namespace Branding
