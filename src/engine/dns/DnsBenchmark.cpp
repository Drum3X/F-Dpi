#include "include/engine/dns/DnsBenchmark.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSet>
#include <QThread>
#include <QUdpSocket>
#include <QUrl>

#include "include/dns/AutoDns.h"
#include "include/dns/DnsWire.h"
#include "include/engine/ThreadCheck.h"
#include "include/global/Utils.hpp"
#include "include/sys/SystemDns.hpp"

namespace Engine {
    namespace {
        // Popular, so every resolver has them cached: the time is the network path plus the server, not its upstream.
        const QStringList kDnsBenchmarkLatencyDomains = {QStringLiteral("google.com"), QStringLiteral("wikipedia.org"),
                                                         QStringLiteral("cloudflare.com"), QStringLiteral("microsoft.com"),
                                                         QStringLiteral("amazon.com")};
        const QString kDnsBenchmarkWarmupDomain = QStringLiteral("example.com");
        const QString kDnsBenchmarkPlainServer = QStringLiteral("1.1.1.1");

        struct DnsBenchmarkLookup {
            bool ok = false;
            Dns::Answer answer;
            qint64 ms = -1;
            QString error;
        };

        QStringList dnsBenchmarkIpv4(const Dns::Answer &answer) {
            QStringList list;
            for (const auto &address : answer.addresses) {
                if (address.protocol() == QAbstractSocket::IPv4Protocol) list << address.toString();
            }
            list.sort();
            return list;
        }

        // RFC 8484 POST over manager's (reused) connection.
        DnsBenchmarkLookup dnsBenchmarkDoh(QNetworkAccessManager &manager, const QString &url, const QString &domain, int timeoutMs) {
            DnsBenchmarkLookup lookup;
            QNetworkRequest request{QUrl(url)};
            request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/dns-message"));
            request.setRawHeader("accept", "application/dns-message");
            request.setTransferTimeout(timeoutMs);
            QElapsedTimer timer;
            timer.start();
            QEventLoop loop;
            QNetworkReply *reply = manager.post(request, Dns::BuildQuery(domain, Dns::A));
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();
            lookup.ms = timer.elapsed();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError) {
                lookup.error = reply->error() == QNetworkReply::OperationCanceledError ? QCoreApplication::translate("AutoDns", "timed out")
                                                                                       : reply->errorString();
            } else if (status != 200) {
                lookup.error = QStringLiteral("HTTP %1").arg(status);
            } else {
                lookup.ok = Dns::ParseResponse(reply->readAll(), &lookup.answer, &lookup.error);
            }
            reply->deleteLater();
            return lookup;
        }

        DnsBenchmarkLookup dnsBenchmarkUdp(const QString &server, const QString &domain, int timeoutMs) {
            DnsBenchmarkLookup lookup;
            QUdpSocket socket;
            socket.setProxy(QNetworkProxy::NoProxy);
            const QHostAddress address(server);
            if (address.isNull() || !socket.bind(address.protocol() == QAbstractSocket::IPv6Protocol ? QHostAddress::AnyIPv6 : QHostAddress::AnyIPv4, 0)) {
                lookup.error = QStringLiteral("socket");
                return lookup;
            }
            const auto id = static_cast<quint16>(QRandomGenerator::global()->bounded(1, 65536));
            QElapsedTimer timer;
            timer.start();
            socket.writeDatagram(Dns::BuildQuery(domain, Dns::A, id), address, 53);
            while (timer.elapsed() < timeoutMs) {
                if (!socket.waitForReadyRead(static_cast<int>(timeoutMs - timer.elapsed()))) break;
                while (socket.hasPendingDatagrams()) {
                    QByteArray data(static_cast<int>(socket.pendingDatagramSize()), '\0');
                    socket.readDatagram(data.data(), data.size());
                    Dns::Answer answer;
                    if (Dns::ParseResponse(data, &answer) && answer.id == id) {
                        lookup.ms = timer.elapsed();
                        lookup.answer = answer;
                        lookup.ok = true;
                        return lookup;
                    }
                }
            }
            lookup.error = QCoreApplication::translate("AutoDns", "timed out");
            return lookup;
        }

        struct DnsBenchmarkMeasure {
            DnsServerScore score;
            // Test domain -> IPv4 answers.
            QHash<QString, QStringList> answers;
        };

        DnsBenchmarkMeasure dnsBenchmarkMeasure(const Dns::Provider &provider, const DnsBenchmarkOptions &options, const std::atomic<bool> &cancel) {
            DnsBenchmarkMeasure measure;
            auto &score = measure.score;
            score.id = provider.id;
            score.name = provider.name;
            QNetworkAccessManager manager;
            manager.setProxy(QNetworkProxy::NoProxy);

            const auto warmup = dnsBenchmarkDoh(manager, provider.url, kDnsBenchmarkWarmupDomain, options.timeoutMs);
            if (!warmup.ok) {
                score.note = warmup.error;
                return measure;
            }
            score.reachable = true;
            score.connectMs = static_cast<int>(warmup.ms);

            std::vector<qint64> times;
            const auto domains = kDnsBenchmarkLatencyDomains + options.testDomains;
            for (const auto &domain : domains) {
                if (cancel) break;
                ++score.asked;
                const auto lookup = dnsBenchmarkDoh(manager, provider.url, domain, options.timeoutMs);
                if (!lookup.ok) continue;
                ++score.answered;
                times.push_back(lookup.ms);
                if (!options.testDomains.contains(domain)) continue;
                measure.answers[domain] = dnsBenchmarkIpv4(lookup.answer);
                for (const auto &address : lookup.answer.addresses) {
                    if (Dns::IsBogon(address)) {
                        score.filtered = true;
                        score.note = QCoreApplication::translate("AutoDns", "answers %1 with %2").arg(domain, address.toString());
                    }
                }
            }
            if (!times.empty()) {
                std::sort(times.begin(), times.end());
                score.medianMs = static_cast<int>(times[times.size() / 2]);
            }
            if (score.answered * 2 < score.asked && score.note.isEmpty()) {
                score.note = QCoreApplication::translate("AutoDns", "answered %1 of %2").arg(score.answered).arg(score.asked);
            }
            return measure;
        }

        bool dnsBenchmarkHasBogon(const QStringList &addresses) {
            return std::any_of(addresses.begin(), addresses.end(), [](const QString &address) { return Dns::IsBogon(QHostAddress(address)); });
        }

        bool dnsBenchmarkIntersects(const QStringList &a, const QStringList &b) {
            return std::any_of(a.begin(), a.end(), [&b](const QString &address) { return b.contains(address); });
        }

        // answers: domain -> answer from the checked resolver (absent: no answer). Same IPs for two unrelated blocked sites
        // that encrypted DNS resolves elsewhere is a block page.
        void dnsBenchmarkJudge(QList<DnsDomainCheck> &checks, bool plain) {
            QHash<QString, int> shared;
            for (const auto &check : checks) {
                const auto &answer = plain ? check.plain : check.system;
                if (!answer.isEmpty() && !dnsBenchmarkIntersects(answer, check.secure)) shared[answer.join(',')]++;
            }
            for (auto &check : checks) {
                const auto &answer = plain ? check.plain : check.system;
                auto &verdict = plain ? check.plainVerdict : check.systemVerdict;
                if (verdict == DnsVerdict::Unknown) continue;
                if (answer.isEmpty()) verdict = check.secure.isEmpty() ? DnsVerdict::Unknown : DnsVerdict::NoAnswer;
                else if (dnsBenchmarkHasBogon(answer)) verdict = DnsVerdict::Poisoned;
                else if (check.secure.isEmpty() || dnsBenchmarkIntersects(answer, check.secure)) verdict = DnsVerdict::Clean;
                else if (shared.value(answer.join(',')) >= 2) verdict = DnsVerdict::Poisoned;
                else verdict = DnsVerdict::Different;
            }
        }
    } // namespace

    DnsBenchmark::DnsBenchmark(QObject *parent) : QObject(parent) {
        qRegisterMetaType<Engine::DnsServerScore>();
        qRegisterMetaType<Engine::DnsBenchmarkResult>();
    }

    DnsBenchmark::~DnsBenchmark() {
        m_cancel = true;
    }

    QString DnsBenchmark::VerdictText(DnsVerdict verdict) {
        switch (verdict) {
            case DnsVerdict::Unknown: return QCoreApplication::translate("AutoDns", "not checked");
            case DnsVerdict::Clean: return QCoreApplication::translate("AutoDns", "correct");
            case DnsVerdict::Different: return QCoreApplication::translate("AutoDns", "different (CDN or redirected)");
            case DnsVerdict::Poisoned: return QCoreApplication::translate("AutoDns", "POISONED");
            case DnsVerdict::NoAnswer: return QCoreApplication::translate("AutoDns", "no answer (blocked)");
        }
        return {};
    }

    bool DnsBenchmark::start(const DnsBenchmarkOptions &options) {
        ENGINE_CHECK_MAIN_THREAD();
        if (m_running) return false;
        m_running = true;
        m_cancel = false;
        runOnNewThread([this, options] { run(options); });
        return true;
    }

    void DnsBenchmark::cancel() {
        m_cancel = true;
    }

    void DnsBenchmark::run(const DnsBenchmarkOptions &options) {
        DnsBenchmarkResult result;
        const auto &providers = Dns::Providers();
        emit message(QCoreApplication::translate("AutoDns", "Timing %1 encrypted DNS servers...").arg(providers.size()));

        std::vector<DnsBenchmarkMeasure> measures(providers.size());
        std::vector<QThread *> threads;
        for (int i = 0; i < providers.size(); ++i) {
            threads.push_back(QThread::create([&, i] { measures[i] = dnsBenchmarkMeasure(providers[i], options, m_cancel); }));
            threads.back()->start();
        }
        for (auto *thread : threads) {
            thread->wait();
            delete thread;
        }

        for (const auto &measure : measures) {
            result.servers << measure.score;
            emit serverMeasured(measure.score);
            const auto &score = measure.score;
            if (score.usable() && (result.bestMs < 0 || score.medianMs < result.bestMs)) {
                result.bestId = score.id;
                result.bestMs = score.medianMs;
            }
        }
        result.cancelled = m_cancel;
        if (result.bestId.isEmpty() && !result.cancelled) {
            result.error = QCoreApplication::translate("AutoDns", "No encrypted DNS server answered. The network may block DNS-over-HTTPS, "
                                                                  "or there is no internet connection.");
        }

        if (options.poisonTest && !m_cancel && !options.testDomains.isEmpty()) {
            const auto systemServers = options.systemServers.isEmpty() ? Sys::SystemDns::EffectiveServers() : options.systemServers;
            for (const auto &server : systemServers) {
                if (QHostAddress(server).protocol() == QAbstractSocket::IPv4Protocol) {
                    result.systemServer = server;
                    break;
                }
            }
            emit message(result.systemServer.isEmpty()
                             ? QCoreApplication::translate("AutoDns", "Checking for DNS poisoning (the system DNS server is unknown)...")
                             : QCoreApplication::translate("AutoDns", "Checking for DNS poisoning: system DNS %1 and plain DNS %2 against encrypted DNS...")
                                   .arg(result.systemServer, kDnsBenchmarkPlainServer));
            for (const auto &domain : options.testDomains) {
                if (m_cancel) break;
                DnsDomainCheck check;
                check.domain = domain;
                QSet<QString> secure;
                for (const auto &measure : measures) {
                    if (!measure.score.usable()) continue;
                    for (const auto &address : measure.answers.value(domain)) secure.insert(address);
                }
                check.secure = QStringList(secure.begin(), secure.end());
                check.secure.sort();
                if (!result.systemServer.isEmpty()) {
                    const auto lookup = dnsBenchmarkUdp(result.systemServer, domain, options.timeoutMs);
                    check.systemVerdict = DnsVerdict::Clean;
                    if (lookup.ok) check.system = dnsBenchmarkIpv4(lookup.answer);
                }
                const auto plain = dnsBenchmarkUdp(kDnsBenchmarkPlainServer, domain, options.timeoutMs);
                check.plainVerdict = DnsVerdict::Clean;
                if (plain.ok) check.plain = dnsBenchmarkIpv4(plain.answer);
                result.domains << check;
            }
            dnsBenchmarkJudge(result.domains, false);
            dnsBenchmarkJudge(result.domains, true);
            for (const auto &check : result.domains) {
                if (check.systemVerdict == DnsVerdict::Poisoned || check.systemVerdict == DnsVerdict::NoAnswer) result.systemPoisoned = true;
                if (check.plainVerdict == DnsVerdict::Poisoned || check.plainVerdict == DnsVerdict::NoAnswer) result.plainIntercepted = true;
            }
            result.cancelled = m_cancel;
        }

        QMetaObject::invokeMethod(this, [this, result] {
            m_running = false;
            emit finished(result);
        }, Qt::QueuedConnection);
    }
} // namespace Engine
