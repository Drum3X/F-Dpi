#pragma once

#include <QString>

// F-Dpi (Linux): the privileged core. ThroneCore is made setuid root, as Throne already does for Tun, and runs the DPI
// engine (nfqws, nftables) and the system DNS changes for the GUI. nfqws runs as root from <core dir>/zapret, so that
// directory is given to root too: the core refuses an nfqws another user could replace (core/internal/dpi).
namespace Sys::Linux {
    // One pkexec prompt: chown root:root + chmod 4755 the core, chown -R root:root + go-w the zapret directory.
    // Blocks until pkexec returns; call it off the UI thread. false with *error when it failed or was dismissed.
    bool GrantCorePrivileges(const QString &corePath, const QString &zapretDir, QString *error);
} // namespace Sys::Linux
