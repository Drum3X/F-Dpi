#include "include/engine/dpi/Blockcheck.h"

#include <utility>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QEventLoop>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include "include/database/entities/Profile.h"
#include "include/dpi/BlockcheckStrategies.h"
#include "include/dpi/DpiLaunch.h"
#include "include/engine/ThreadCheck.h"
#include "include/engine/connection/ConnectionManager.h"
#include "include/engine/dpi/DpiSupervisor.h"
#include "include/global/Utils.hpp"
#include "include/sys/KillSwitch.hpp"

namespace Engine {
    namespace {
        enum class BlockcheckProtocol { Http, Tls12, Tls13 };

        struct BlockcheckProbe {
            QString domain;
            QHostAddress address;
            BlockcheckProtocol protocol;
        };

        // winws needs a moment after start before its filter catches new connections.
        constexpr int kBlockcheckWinwsGraceMs = 500;

        QString blockcheckProtocolName(BlockcheckProtocol protocol) {
            switch (protocol) {
                case BlockcheckProtocol::Http: return QStringLiteral("HTTP");
                case BlockcheckProtocol::Tls12: return QStringLiteral("TLS 1.2");
                case BlockcheckProtocol::Tls13: return QStringLiteral("TLS 1.3");
            }
            return {};
        }

        Dpi::Blockcheck::Target blockcheckTarget(BlockcheckProtocol protocol) {
            return protocol == BlockcheckProtocol::Http ? Dpi::Blockcheck::Target::Http : Dpi::Blockcheck::Target::Tls;
        }

        // Cloudflare's DoH JSON API over a pinned IP: a poisoned system resolver cannot hide or redirect the site.
        QHostAddress blockcheckResolveDoh(const QString &domain, int timeoutMs) {
            QNetworkAccessManager manager;
            manager.setProxy(QNetworkProxy::NoProxy);
            QUrl url(QStringLiteral("https://1.1.1.1/dns-query"));
            url.setQuery(QUrlQuery{{QStringLiteral("name"), domain}, {QStringLiteral("type"), QStringLiteral("A")}});
            QNetworkRequest request(url);
            request.setRawHeader("accept", "application/dns-json");
            request.setTransferTimeout(timeoutMs);
            QEventLoop loop;
            QNetworkReply *reply = manager.get(request);
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();
            const auto json = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            for (const auto &answer : json[QStringLiteral("Answer")].toArray()) {
                const auto object = answer.toObject();
                if (object[QStringLiteral("type")].toInt() != 1) continue;
                QHostAddress address(object[QStringLiteral("data")].toString());
                if (address.protocol() == QAbstractSocket::IPv4Protocol) return address;
            }
            return {};
        }

        QHostAddress blockcheckResolveSystem(const QString &domain) {
            for (const auto &address : QHostInfo::fromName(domain).addresses()) {
                if (address.protocol() == QAbstractSocket::IPv4Protocol) return address;
            }
            return {};
        }

        // The handshake must complete with a certificate valid for the site: an ISP block page fails verification.
        bool blockcheckTls(const BlockcheckProbe &probe, int timeoutMs, QString *why) {
            QSslSocket socket;
            socket.setProxy(QNetworkProxy::NoProxy);
            auto config = socket.sslConfiguration();
            config.setProtocol(probe.protocol == BlockcheckProtocol::Tls12 ? QSsl::TlsV1_2 : QSsl::TlsV1_3OrLater);
            socket.setSslConfiguration(config);
            socket.connectToHostEncrypted(probe.address.toString(), 443, probe.domain);
            if (!socket.waitForEncrypted(timeoutMs)) {
                *why = socket.error() == QAbstractSocket::SocketTimeoutError ? QCoreApplication::translate("Blockcheck", "timed out") : socket.errorString();
                socket.abort();
                return false;
            }
            socket.disconnectFromHost();
            return true;
        }

        // Any answer counts unless it is a redirect to another site (the usual ISP block page) or never comes.
        bool blockcheckHttp(const BlockcheckProbe &probe, int timeoutMs, QString *why) {
            QTcpSocket socket;
            socket.setProxy(QNetworkProxy::NoProxy);
            socket.connectToHost(probe.address, 80);
            if (!socket.waitForConnected(timeoutMs)) {
                *why = socket.errorString();
                return false;
            }
            socket.write("GET / HTTP/1.1\r\nHost: " + probe.domain.toLatin1() +
                         "\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\nAccept: */*\r\nConnection: close\r\n\r\n");
            socket.waitForBytesWritten(timeoutMs);
            QByteArray data;
            QElapsedTimer timer;
            timer.start();
            while (!data.contains("\r\n\r\n") && timer.elapsed() < timeoutMs) {
                if (!socket.waitForReadyRead(static_cast<int>(timeoutMs - timer.elapsed()))) break;
                data += socket.readAll();
            }
            socket.abort();
            const auto lines = QString::fromLatin1(data).split(QStringLiteral("\r\n"));
            const auto status = lines.value(0).section(' ', 1, 1).toInt();
            if (!lines.value(0).startsWith(QStringLiteral("HTTP/")) || status == 0) {
                *why = data.isEmpty() ? QCoreApplication::translate("Blockcheck", "no answer") : QCoreApplication::translate("Blockcheck", "not an HTTP answer");
                return false;
            }
            if (status < 300 || status >= 400) return true;
            for (const auto &line : lines) {
                if (!line.startsWith(QStringLiteral("location:"), Qt::CaseInsensitive)) continue;
                const auto host = QUrl(line.section(':', 1).trimmed()).host().toLower();
                if (host.isEmpty() || host == probe.domain || host.endsWith('.' + probe.domain) || probe.domain.endsWith('.' + host)) return true;
                *why = QCoreApplication::translate("Blockcheck", "redirected to %1").arg(host);
                return false;
            }
            return true;
        }

        bool blockcheckRun(const BlockcheckProbe &probe, int timeoutMs, QString *why) {
            return probe.protocol == BlockcheckProtocol::Http ? blockcheckHttp(probe, timeoutMs, why) : blockcheckTls(probe, timeoutMs, why);
        }
    } // namespace

    Blockcheck::Blockcheck(ConnectionManager *connection, QObject *parent) : QObject(parent), m_connection(connection) {
        qRegisterMetaType<Engine::BlockcheckResult>();
    }

    Blockcheck::~Blockcheck() {
        m_cancel = true;
    }

    bool Blockcheck::start(const BlockcheckOptions &options) {
        ENGINE_CHECK_MAIN_THREAD();
        if (m_running) return false;
        if (Sys::KillSwitch::instance()->state() != Sys::KillSwitch::State::Disabled) {
            BlockcheckResult result;
            result.error = QCoreApplication::translate("Blockcheck", "Turn the kill switch off first: with it on, nothing reaches the sites without a proxy.");
            QTimer::singleShot(0, this, [this, result] { emit finished(result); });
            return true;
        }
        m_running = true;
        m_cancel = false;
        m_connection->setExclusiveTask(QCoreApplication::translate("Blockcheck", "Blockcheck is running; profiles start again when it ends."));
        const auto running = m_connection->runningSnapshot();
        m_resumeProfile = running != nullptr ? running->id : -1;
        if (running != nullptr) {
            emit message(QCoreApplication::translate("Blockcheck", "Stopping %1 for the scan; it starts again afterwards.").arg(running->outbound->DisplayTypeAndName()));
            // Not manual: the started profile is kept, so it is the one resumed.
            m_connection->stop({false, true, false, false});
        }
        if (m_dpi == nullptr) {
            m_dpi = new DpiSupervisor(this, false);
            m_dpi->setQuiet(true);
        }
        runOnNewThread([this, options] { run(options); });
        return true;
    }

    void Blockcheck::cancel() {
        m_cancel = true;
    }

    void Blockcheck::run(const BlockcheckOptions &options) {
        BlockcheckResult result;
        const auto done = [this, &result] {
            QMetaObject::invokeMethod(this, [this, result] { finish(result); }, Qt::QueuedConnection);
        };

        // 1. Addresses, before any winws runs: its desync would also hit the DoH request.
        QList<BlockcheckProbe> probes;
        for (const auto &raw : options.domains) {
            const auto domain = raw.trimmed().toLower();
            if (domain.isEmpty()) continue;
            if (m_cancel) break;
            QHostAddress address = blockcheckResolveDoh(domain, options.timeoutMs);
            QString via = QStringLiteral("DoH");
            if (address.isNull()) {
                address = blockcheckResolveSystem(domain);
                via = QCoreApplication::translate("Blockcheck", "system DNS");
            }
            if (address.isNull()) {
                emit message(QCoreApplication::translate("Blockcheck", "%1: cannot resolve, skipped.").arg(domain));
                continue;
            }
            emit message(QCoreApplication::translate("Blockcheck", "%1 → %2 (%3)").arg(domain, address.toString(), via));
            if (options.http) probes << BlockcheckProbe{domain, address, BlockcheckProtocol::Http};
            if (options.tls12 && QSslSocket::isProtocolSupported(QSsl::TlsV1_2)) probes << BlockcheckProbe{domain, address, BlockcheckProtocol::Tls12};
            if (options.tls13 && QSslSocket::isProtocolSupported(QSsl::TlsV1_3OrLater)) probes << BlockcheckProbe{domain, address, BlockcheckProtocol::Tls13};
        }
        if (m_cancel) {
            result.cancelled = true;
            return done();
        }
        if (probes.isEmpty()) {
            result.error = QCoreApplication::translate("Blockcheck", "No site could be resolved; check the names and the connection.");
            return done();
        }

        // 2. Which probes fail without bypass.
        QList<BlockcheckProbe> blocked;
        for (const auto &probe : probes) {
            QString why;
            const bool ok = blockcheckRun(probe, options.timeoutMs, &why);
            emit message(ok ? QCoreApplication::translate("Blockcheck", "%1 %2: works without bypass").arg(probe.domain, blockcheckProtocolName(probe.protocol))
                            : QCoreApplication::translate("Blockcheck", "%1 %2: blocked (%3)").arg(probe.domain, blockcheckProtocolName(probe.protocol), why));
            if (!ok) blocked << probe;
            if (m_cancel) {
                result.cancelled = true;
                return done();
            }
        }
        for (const auto &probe : blocked) {
            (probe.protocol == BlockcheckProtocol::Http ? result.httpBlocked : result.tlsBlocked)++;
        }
        if (blocked.isEmpty()) {
            emit message(QCoreApplication::translate("Blockcheck", "Nothing is blocked on these sites; no bypass is needed."));
            return done();
        }

        // 3. Strategies, one target at a time.
        using Dpi::Blockcheck::Target;
        QList<Target> targets;
        if (result.httpBlocked > 0) targets << Target::Http;
        if (result.tlsBlocked > 0) targets << Target::Tls;
        QHash<int, QList<QString>> strategies;
        int total = 0;
        for (const auto target : targets) {
            strategies[int(target)] = options.deep ? Dpi::Blockcheck::DeepStrategies(target) : Dpi::Blockcheck::QuickStrategies(target);
            total += strategies[int(target)].size();
        }
        int tried = 0;
        for (const auto target : targets) {
            const QString targetName = target == Target::Http ? QStringLiteral("HTTP") : QStringLiteral("TLS");
            QList<BlockcheckProbe> targetProbes;
            for (const auto &probe : blocked) {
                if (blockcheckTarget(probe.protocol) == target) targetProbes << probe;
            }
            QString best;
            int bestPassed = 0;
            for (const auto &strategy : strategies[int(target)]) {
                if (m_cancel) break;
                emit progress(tried, total, targetName + QStringLiteral(": ") + strategy);
                ++tried;
                Dpi::Launch launch{Dpi::WinwsPath(), Dpi::Blockcheck::TrialArguments(target, strategy), Dpi::ZapretDir()};
                QString error;
                if (!m_dpi->start(launch, &error, kBlockcheckWinwsGraceMs)) {
                    // WinDivert cannot open (no administrator, blocked driver): every other strategy would fail the same way.
                    if (error.contains(QStringLiteral("windivert"), Qt::CaseInsensitive)) {
                        result.error = error;
                        return done();
                    }
                    emit strategyTested(targetName, strategy, -1, targetProbes.size());
                    continue;
                }
                int passed = 0;
                for (const auto &probe : targetProbes) {
                    QString why;
                    if (blockcheckRun(probe, options.timeoutMs, &why)) ++passed;
                    if (m_cancel) break;
                }
                m_dpi->stop();
                emit strategyTested(targetName, strategy, passed, targetProbes.size());
                if (passed > bestPassed) {
                    best = strategy;
                    bestPassed = passed;
                }
                if (!options.deep && passed == targetProbes.size()) break;
            }
            m_dpi->stop();
            if (target == Target::Http) {
                result.bestHttp = best;
                result.bestHttpPassed = bestPassed;
            } else {
                result.bestTls = best;
                result.bestTlsPassed = bestPassed;
            }
            if (m_cancel) break;
        }
        emit progress(total, total, {});
        result.cancelled = m_cancel;
        if (!result.bestHttp.isEmpty() || !result.bestTls.isEmpty()) {
            result.strategy = Dpi::Blockcheck::ComposeStrategy(result.bestHttp, result.bestTls);
            result.wfTcp = Dpi::Blockcheck::ComposePorts(result.bestHttp, result.bestTls);
            result.wfUdp = QStringLiteral("443");
        }
        done();
    }

    void Blockcheck::finish(const BlockcheckResult &result) {
        ENGINE_CHECK_MAIN_THREAD();
        m_running = false;
        m_connection->setExclusiveTask({});
        if (const int resume = std::exchange(m_resumeProfile, -1); resume >= 0) {
            m_connection->start(StartRequest{resume, false, m_connection->nextStartSerial()});
        }
        emit finished(result);
    }
} // namespace Engine
