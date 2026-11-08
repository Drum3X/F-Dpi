#pragma once

#include <QString>
#include <QStringList>

namespace Configs {
    class dpi;
}

namespace Dpi {
    // How to run the DPI engine for one profile: winws on Windows (it captures with its own --wf-* options), nfqws on Linux,
    // which the privileged core runs behind an nftables queue rule for the given ports (core/internal/dpi).
    struct Launch {
        QString program;
        QStringList arguments;
        QString workingDir;
        // Linux: the ports whose traffic goes through nfqws ("80,443", "50000-50100").
        QStringList tcpPorts;
        QStringList udpPorts;
    };

    // The bundled zapret directory next to the executable (deployment/<os>/zapret).
    QString ZapretDir();

    // winws.exe on Windows, nfqws elsewhere.
    QString EnginePath();
    QString WinwsPath();

    // The program and its arguments on one line, for the log and the editor preview; on Linux with the queued ports.
    QString DisplayCommandLine(const Launch &launch);

    // Where the site list files of DPI profiles are written: <config dir>/dpi.
    QString ListDir();

    // winws is a Cygwin program: it treats a backslash in an unquoted argument as an escape, so every value with a backslash or
    // a space is passed quoted (--opt="C:\path"), as zapret's own .cmd presets do. Arguments carry no quotes of their own.
    QString WindowsCommandLine(const QStringList &arguments);

    // A profile's list file: kind is hostlist, exclude or autohostlist.
    QString ListPath(const QString &kind, int profileId);

    // Writes the profile's site lists and builds the engine's command line. False with a translated error when it cannot run.
    // dryRun builds the same command line without touching any file (editor preview).
    bool BuildLaunch(const Configs::dpi &config, int profileId, Launch *out, QString *error, bool dryRun = false);
} // namespace Dpi
