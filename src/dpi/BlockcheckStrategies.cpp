#include "include/dpi/BlockcheckStrategies.h"

#include <QStringList>

namespace Dpi::Blockcheck {
    QList<QString> QuickStrategies(Target target) {
        if (target == Target::Http) {
            return {
                QStringLiteral("--hostcase"),
                QStringLiteral("--dpi-desync=multisplit --dpi-desync-split-pos=method+2"),
                QStringLiteral("--dpi-desync=multidisorder --dpi-desync-split-pos=method+2"),
                QStringLiteral("--dpi-desync=fake --dpi-desync-fooling=md5sig"),
                QStringLiteral("--dpi-desync=fake,multisplit --dpi-desync-split-pos=method+2 --dpi-desync-fooling=md5sig"),
                QStringLiteral("--dpi-desync=fake,fakedsplit --dpi-desync-autottl=2 --dpi-desync-fooling=md5sig"),
                QStringLiteral("--dpi-desync=fake,multidisorder --dpi-desync-split-pos=1,midsld --dpi-desync-fooling=badseq"),
            };
        }
        return {
            QStringLiteral("--dpi-desync=multisplit --dpi-desync-split-pos=1"),
            QStringLiteral("--dpi-desync=multisplit --dpi-desync-split-pos=midsld"),
            QStringLiteral("--dpi-desync=multisplit --dpi-desync-split-pos=1,midsld"),
            QStringLiteral("--dpi-desync=multidisorder --dpi-desync-split-pos=1,midsld"),
            QStringLiteral("--dpi-desync=fake --dpi-desync-fooling=badseq"),
            QStringLiteral("--dpi-desync=fake --dpi-desync-fooling=md5sig"),
            QStringLiteral("--dpi-desync=fake --dpi-desync-autottl=2"),
            QStringLiteral("--dpi-desync=fake --dpi-desync-ttl=4"),
            QStringLiteral("--dpi-desync=fake,multisplit --dpi-desync-split-pos=1 --dpi-desync-fooling=badseq"),
            QStringLiteral("--dpi-desync=fake,multisplit --dpi-desync-split-pos=midsld --dpi-desync-fooling=md5sig"),
            QStringLiteral("--dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-ttl=4"),
            QStringLiteral("--dpi-desync=fake,multidisorder --dpi-desync-split-pos=1,midsld --dpi-desync-fooling=md5sig"),
            QStringLiteral("--dpi-desync=fake,multidisorder --dpi-desync-split-pos=midsld --dpi-desync-repeats=6 --dpi-desync-fooling=badseq,md5sig"),
            QStringLiteral("--dpi-desync=fake,multidisorder --dpi-desync-split-pos=1,midsld --dpi-desync-repeats=11 --dpi-desync-fooling=md5sig "
                           "--dpi-desync-fake-tls-mod=rnd,dupsid,sni=www.google.com"),
            QStringLiteral("--dpi-desync=fakedsplit --dpi-desync-split-pos=1 --dpi-desync-fooling=badseq"),
            QStringLiteral("--dpi-desync=fakeddisorder --dpi-desync-split-pos=midsld --dpi-desync-fooling=md5sig"),
            QStringLiteral("--dpi-desync=fake,fakedsplit --dpi-desync-split-pos=1 --dpi-desync-autottl=2 --dpi-desync-fooling=md5sig"),
        };
    }

    QList<QString> DeepStrategies(Target target) {
        const bool http = target == Target::Http;
        const QStringList positions = http
            ? QStringList{QStringLiteral("method+2"), QStringLiteral("midsld"), QStringLiteral("1,method+2"), QStringLiteral("host+1")}
            : QStringList{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("midsld"), QStringLiteral("1,midsld"), QStringLiteral("sniext+1")};
        const QStringList foolings = {
            QStringLiteral("--dpi-desync-fooling=md5sig"), QStringLiteral("--dpi-desync-fooling=badseq"),
            QStringLiteral("--dpi-desync-fooling=badsum"), QStringLiteral("--dpi-desync-fooling=datanoack"),
            QStringLiteral("--dpi-desync-autottl=2"), QStringLiteral("--dpi-desync-ttl=3"), QStringLiteral("--dpi-desync-ttl=5"),
        };
        QList<QString> out;
        if (http) out << QStringLiteral("--hostcase") << QStringLiteral("--hostspell=hoSt") << QStringLiteral("--methodeol");
        // Splits alone send no fake packet, so they go first: they are the gentlest on the connection.
        for (const auto *mode : {"multisplit", "multidisorder"}) {
            for (const auto &pos : positions) out << QStringLiteral("--dpi-desync=%1 --dpi-desync-split-pos=%2").arg(QLatin1String(mode), pos);
        }
        for (const auto &fooling : foolings) {
            for (const auto *repeats : {"", " --dpi-desync-repeats=6"}) {
                out << QStringLiteral("--dpi-desync=fake %1%2").arg(fooling, QLatin1String(repeats));
            }
        }
        for (const auto *mode : {"fake,multisplit", "fake,multidisorder", "fakedsplit", "fakeddisorder", "fake,fakedsplit"}) {
            for (const auto &pos : positions) {
                for (const auto &fooling : foolings) {
                    out << QStringLiteral("--dpi-desync=%1 --dpi-desync-split-pos=%2 %3").arg(QLatin1String(mode), pos, fooling);
                }
            }
        }
        return out;
    }

    QStringList TrialArguments(Target target, const QString &strategy) {
        const QString port = target == Target::Http ? QStringLiteral("80") : QStringLiteral("443");
        QStringList args;
#ifdef Q_OS_WIN
        args << QStringLiteral("--wf-tcp=") + port;
#endif
        args << QStringLiteral("--filter-tcp=") + port;
        args << strategy.split(' ', Qt::SkipEmptyParts);
        return args;
    }

    Launch TrialLaunch(Target target, const QString &strategy) {
        Launch launch{EnginePath(), TrialArguments(target, strategy), ZapretDir(), {}, {}};
#ifndef Q_OS_WIN
        launch.tcpPorts << (target == Target::Http ? QStringLiteral("80") : QStringLiteral("443"));
#endif
        return launch;
    }

    bool IsEngineSetupError(const QString &error) {
        static const QStringList markers = {
            QStringLiteral("windivert"),           // Windows: no administrator, blocked driver
            QStringLiteral("root privileges"),     // Linux: the core is not setuid root
            QStringLiteral("nftables"),            // no nftables / queue support in the kernel
            QStringLiteral("nfqws was not found"), // missing or untrusted engine
            QStringLiteral("could replace nfqws"),
            QStringLiteral("must be an executable owned by root"),
            QStringLiteral("nfq_"),                // nfqws could not open the queue
            QStringLiteral("core is not running"),
        };
        for (const auto &marker : markers) {
            if (error.contains(marker, Qt::CaseInsensitive)) return true;
        }
        return false;
    }

    QString ComposeStrategy(const QString &http, const QString &tls) {
        QStringList blocks;
        if (!http.isEmpty()) blocks << QStringLiteral("--filter-tcp=80 ") + http;
        if (!tls.isEmpty()) blocks << QStringLiteral("--filter-tcp=443 ") + tls;
#ifdef Q_OS_WIN
        blocks << QStringLiteral("--filter-udp=443 --filter-l7=quic --dpi-desync=fake --dpi-desync-repeats=11 "
                                 "--dpi-desync-fake-quic=\"%ZAPRET%\\files\\quic_initial_www_google_com.bin\"");
#else
        blocks << QStringLiteral("--filter-udp=443 --filter-l7=quic --dpi-desync=fake --dpi-desync-repeats=11 "
                                 "--dpi-desync-fake-quic=\"%ZAPRET%/files/quic_initial_www_google_com.bin\"");
#endif
        return QStringLiteral("# Blockcheck\n") + blocks.join(QStringLiteral("\n--new\n")) + QLatin1Char('\n');
    }

    QString ComposePorts(const QString &http, const QString &tls) {
        QStringList ports;
        if (!http.isEmpty()) ports << QStringLiteral("80");
        if (!tls.isEmpty() || http.isEmpty()) ports << QStringLiteral("443");
        return ports.join(',');
    }
} // namespace Dpi::Blockcheck
