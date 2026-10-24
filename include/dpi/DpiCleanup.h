#pragma once

#include <QString>

namespace Dpi {
    // F-Dpi "-cleanup": what a stopped F-Dpi may have left behind on Windows, for the uninstaller and updates (which stop it
    // first, without the graceful exit that would do this). Puts back the Windows DNS settings Otomatik DNS changed
    // (configDir/dpi/dns-backup.json) and unloads the WinDivert driver when it was loaded from this install's zapret folder.
    // Writes what it did to stderr; returns the process exit code (0, or 1 when a step failed).
    int RunCleanup(const QString &configDir);
} // namespace Dpi
