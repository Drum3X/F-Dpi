#pragma once

#include <QString>
#include <QStringList>

namespace Configs {
    class dpi;
}

namespace Dpi {
    // How to run winws for one profile.
    struct Launch {
        QString program;
        QStringList arguments;
        QString workingDir;
    };

    // The bundled winws directory next to the executable (deployment/<os>/zapret).
    QString ZapretDir();

    QString WinwsPath();

    // Where the site list files of DPI profiles are written: <config dir>/dpi.
    QString ListDir();

    // winws is a Cygwin program: it treats a backslash in an unquoted argument as an escape, so every value with a backslash or
    // a space is passed quoted (--opt="C:\path"), as zapret's own .cmd presets do. Arguments carry no quotes of their own.
    QString WindowsCommandLine(const QStringList &arguments);

    // Writes the profile's site lists and builds the winws command line. False with a translated error when it cannot run.
    bool BuildLaunch(const Configs::dpi &config, int profileId, Launch *out, QString *error);
} // namespace Dpi
