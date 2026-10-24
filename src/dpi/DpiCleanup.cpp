#include "include/dpi/DpiCleanup.h"

#include <cstdio>

#include <QDir>
#include <QFile>

#include "include/dns/AutoDns.h"
#include "include/dpi/DpiLaunch.h"
#include "include/sys/ChildProcess.hpp"

namespace Dpi {
    int RunCleanup(const QString &configDir) {
        int code = 0;
        const auto say = [](const QString &line) { std::fputs(qPrintable(line + QLatin1Char('\n')), stderr); };

        const auto backup = QDir(configDir).filePath(QStringLiteral("dpi/dns-backup.json"));
        if (QFile::exists(backup)) {
            QString error;
            if (Dns::RestoreSystemDnsBackup(backup, &error)) {
                say(QStringLiteral("Windows DNS settings restored."));
            } else {
                say(QStringLiteral("Could not restore the Windows DNS settings: %1").arg(error));
                code = 1;
            }
        }

        QString note;
        if (Sys::StopDriverLoadedFrom(QStringLiteral("WinDivert"), ZapretDir(), &note)) {
            say(QStringLiteral("WinDivert driver stopped."));
        } else if (!note.isEmpty()) {
            say(QStringLiteral("WinDivert driver left loaded: %1").arg(note));
        }
        return code;
    }
} // namespace Dpi
