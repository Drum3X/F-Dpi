#include "include/sys/linux/FdpiPrivileges.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace Sys::Linux {
    namespace {
        // Paths travel as positional parameters, never inside the script text, so no quoting can break it.
        // chown clears the setuid bit, so chmod comes after it; -h changes a symlink itself, never what it points to.
        constexpr auto kFdpiPrivilegesScript =
            "set -e\n"
            "core=\"$1\"; zapret=\"$2\"\n"
            "chown root:root \"$core\"\n"
            "chmod 4755 \"$core\"\n"
            "if [ -n \"$zapret\" ] && [ -d \"$zapret\" ]; then\n"
            "  chown -R -h root:root \"$zapret\"\n"
            "  chmod -R u+rwX,go+rX,go-w \"$zapret\"\n"
            "fi\n";
    } // namespace

    bool GrantCorePrivileges(const QString &corePath, const QString &zapretDir, QString *error) {
        const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        if (pkexec.isEmpty()) {
            if (error) *error = QStringLiteral("pkexec was not found; install polkit's pkexec");
            return false;
        }
        if (!QFileInfo(corePath).isFile()) {
            if (error) *error = QStringLiteral("the core was not found at %1").arg(corePath);
            return false;
        }
        const QString dir = QFileInfo(zapretDir).isDir() ? QFileInfo(zapretDir).canonicalFilePath() : QString();

        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(pkexec, {QStringLiteral("/bin/sh"), QStringLiteral("-c"), QString::fromLatin1(kFdpiPrivilegesScript),
                         QStringLiteral("sh"), QFileInfo(corePath).canonicalFilePath(), dir});
        // The password prompt waits for the user.
        if (!p.waitForStarted(5000) || !p.waitForFinished(-1)) {
            if (error) *error = QStringLiteral("pkexec could not be run: %1").arg(p.errorString());
            return false;
        }
        if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) return true;
        if (error) {
            const int code = p.exitCode();
            // pkexec: 126 = the dialog was dismissed or authorization failed, 127 = not authorized.
            if (code == 126 || code == 127) *error = QStringLiteral("authorization was not given (pkexec %1)").arg(code);
            else *error = QStringLiteral("granting the core privileges failed (exit %1): %2")
                              .arg(code).arg(QString::fromLocal8Bit(p.readAll()).trimmed());
        }
        return false;
    }
} // namespace Sys::Linux
