#include "include/dpi/DpiLaunch.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "include/configs/outbounds/dpi.h"

namespace Dpi {
    namespace {
        bool dpiLaunchWriteList(const QString &path, const QStringList &hosts, QString *error) {
            QSaveFile file(path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(hosts.join('\n').toUtf8());
                file.write("\n");
                if (file.commit()) return true;
            }
            *error = QCoreApplication::translate("Dpi", "Cannot write the site list %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
            return false;
        }

        bool dpiLaunchHasOption(const QStringList &block, const QString &option) {
            for (const auto &token : block) {
                if (token == option || token.startsWith(option + '=')) return true;
            }
            return false;
        }
    } // namespace

    QString WindowsCommandLine(const QStringList &arguments) {
        QStringList parts;
        for (const auto &argument : arguments) {
            if (!argument.contains(QLatin1Char('\\')) && !argument.contains(QLatin1Char(' '))) {
                parts << argument;
                continue;
            }
            const qsizetype equals = argument.startsWith(QStringLiteral("--")) ? argument.indexOf('=') : -1;
            if (equals > 0) parts << argument.left(equals + 1) + '"' + argument.mid(equals + 1) + '"';
            else parts << '"' + argument + '"';
        }
        return parts.join(' ');
    }

    QString ListPath(const QString &kind, int profileId) {
        // A profile not saved yet (editor preview) has no id.
        const auto id = profileId >= 0 ? QString::number(profileId) : QStringLiteral("new");
        return QDir::toNativeSeparators(QDir(ListDir()).filePath(QStringLiteral("%1-%2.txt").arg(kind, id)));
    }

    QString ZapretDir() {
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("zapret"));
    }

    QString WinwsPath() {
        return QDir(ZapretDir()).filePath(QStringLiteral("winws.exe"));
    }

    QString ListDir() {
        // The data (config) directory is the current directory once Bootstrap opened it, as for throne.db.
        return QDir::current().absoluteFilePath(QStringLiteral("dpi"));
    }

    bool BuildLaunch(const Configs::dpi &config, int profileId, Launch *out, QString *error, bool dryRun) {
#ifndef Q_OS_WIN
        Q_UNUSED(config);
        Q_UNUSED(profileId);
        Q_UNUSED(out);
        Q_UNUSED(dryRun);
        *error = QCoreApplication::translate("Dpi", "DPI Bypass is only available on Windows for now.");
        return false;
#else
        const auto program = WinwsPath();
        if (!QFileInfo::exists(program)) {
            *error = QCoreApplication::translate("Dpi", "winws.exe was not found at %1. Reinstall F-Dpi.").arg(QDir::toNativeSeparators(program));
            return false;
        }
        const auto strategyError = Configs::Checks::DpiStrategy(config.strategy);
        if (!strategyError.isEmpty()) {
            *error = strategyError;
            return false;
        }

        const QDir lists(ListDir());
        if (!dryRun && !lists.mkpath(QStringLiteral("."))) {
            *error = QCoreApplication::translate("Dpi", "Cannot create the folder %1.").arg(QDir::toNativeSeparators(lists.path()));
            return false;
        }
        const auto listPath = [&](const char *kind) { return ListPath(QLatin1String(kind), profileId); };
        const auto writeList = [&](const char *kind, const QStringList &hosts) {
            return dryRun || dpiLaunchWriteList(listPath(kind), hosts, error);
        };

        const auto hosts = Configs::dpi::NormalizeHosts(config.hostlist);
        const auto excluded = Configs::dpi::NormalizeHosts(config.exclude);
        QStringList split;
        if (config.mode == Configs::dpi::ModeHostlist || (config.mode == Configs::dpi::ModeAutoHostlist && !hosts.isEmpty())) {
            if (!writeList("hostlist", hosts)) return false;
            split << QStringLiteral("--hostlist=") + listPath("hostlist");
        }
        if (config.mode == Configs::dpi::ModeAutoHostlist) {
            // winws adds the hosts it finds blocked to this file, so it is kept between runs.
            const auto autoPath = listPath("autohostlist");
            if (!dryRun && !QFileInfo::exists(autoPath)) {
                QFile seed(autoPath);
                if (!seed.open(QIODevice::WriteOnly)) {
                    *error = QCoreApplication::translate("Dpi", "Cannot write the site list %1: %2").arg(autoPath, seed.errorString());
                    return false;
                }
            }
            split << QStringLiteral("--hostlist-auto=") + autoPath;
        }
        if (!excluded.isEmpty()) {
            if (!writeList("exclude", excluded)) return false;
            split << QStringLiteral("--hostlist-exclude=") + listPath("exclude");
        }

        const auto zapret = QDir::toNativeSeparators(ZapretDir());
        auto tokens = config.StrategyTokens();
        for (auto &token : tokens) token.replace(QStringLiteral("%ZAPRET%"), zapret, Qt::CaseInsensitive);

        QStringList args;
        const bool ownCapture = std::any_of(tokens.begin(), tokens.end(), [](const QString &t) { return t.startsWith(QStringLiteral("--wf-")); });
        if (!ownCapture) {
            if (!config.wfTcp.trimmed().isEmpty()) args << QStringLiteral("--wf-tcp=") + config.wfTcp.simplified().remove(' ');
            if (!config.wfUdp.trimmed().isEmpty()) args << QStringLiteral("--wf-udp=") + config.wfUdp.simplified().remove(' ');
        }

        // The split-bypass options are per profile block, so every block between --new separators gets them.
        QList<QStringList> blocks{{}};
        for (const auto &token : tokens) {
            if (token == QStringLiteral("--new")) blocks.append(QStringList());
            else blocks.last() << token;
        }
        bool first = true;
        for (auto &block : blocks) {
            if (block.isEmpty()) continue;
            for (const auto &option : split) {
                if (!dpiLaunchHasOption(block, option.section('=', 0, 0))) block << option;
            }
            if (!std::exchange(first, false)) args << QStringLiteral("--new");
            args << block;
        }

        out->program = program;
        out->arguments = args;
        out->workingDir = ZapretDir();
        return true;
#endif
    }
} // namespace Dpi
