#pragma once

#include <atomic>

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

namespace Engine {
    struct DnsBenchmarkOptions {
        // Blocked sites: the poisoning test resolves them, and a server answering them with a bogus address is filtering.
        QStringList testDomains;
        bool poisonTest = true;
        // What the system resolves with; empty: the adapters' current servers. Set while F-Dpi has replaced them.
        QStringList systemServers;
        int timeoutMs = 3000;
    };

    struct DnsServerScore {
        QString id;
        QString name;
        bool reachable = false;
        // Answered a test domain with a bogus address (0.0.0.0, a private range...): it filters, never picked.
        bool filtered = false;
        // The first query, TLS handshake included.
        int connectMs = -1;
        // Median of the queries after it, over the open connection: what browsing feels.
        int medianMs = -1;
        int answered = 0;
        int asked = 0;
        QString note;

        [[nodiscard]] bool usable() const { return reachable && !filtered && medianMs >= 0 && answered * 2 >= asked; }
    };

    enum class DnsVerdict {
        // Not checked (no server to ask).
        Unknown,
        // Shares an address with the encrypted answers.
        Clean,
        // Other addresses, none of them bogus: a CDN picking another site, or a quieter redirection.
        Different,
        // A bogus address, or the same address for unrelated blocked sites (a block page).
        Poisoned,
        // No answer, an error or "no such domain" while encrypted DNS resolves it.
        NoAnswer,
    };

    struct DnsDomainCheck {
        QString domain;
        // Union of the usable DoH servers' IPv4 answers.
        QStringList secure;
        QStringList system;
        DnsVerdict systemVerdict = DnsVerdict::Unknown;
        // Plain DNS (UDP 53) to 1.1.1.1: tampering on the way when it disagrees with the same server over DoH.
        QStringList plain;
        DnsVerdict plainVerdict = DnsVerdict::Unknown;
    };

    struct DnsBenchmarkResult {
        bool cancelled = false;
        QString error;
        QList<DnsServerScore> servers;
        // Fastest usable server; empty when none answered.
        QString bestId;
        int bestMs = -1;
        QString systemServer;
        QList<DnsDomainCheck> domains;
        bool systemPoisoned = false;
        bool plainIntercepted = false;
    };

    // F-Dpi Otomatik DNS benchmark: times every Dns::Providers() server over DoH (in parallel, no proxy), drops the
    // unreachable and filtering ones, and picks the fastest. The poisoning test compares, for blocked sites, the system
    // resolver's and plain DNS's answers with the encrypted ones. Runs on worker threads; signals arrive on the main thread.
    // Like Blockcheck, the owner must not delete it while running(): cancel() and wait for finished().
    class DnsBenchmark final : public QObject {
        Q_OBJECT

    public:
        explicit DnsBenchmark(QObject *parent = nullptr);
        ~DnsBenchmark() override;

        // Main thread. False when already running.
        bool start(const DnsBenchmarkOptions &options);

        // Any thread: stops after the current step.
        void cancel();

        [[nodiscard]] bool running() const { return m_running; }

        static QString VerdictText(DnsVerdict verdict);

    signals:
        void message(const QString &line);
        void serverMeasured(const Engine::DnsServerScore &score);
        void finished(const Engine::DnsBenchmarkResult &result);

    private:
        void run(const DnsBenchmarkOptions &options);

        bool m_running = false;
        std::atomic<bool> m_cancel{false};
    };
} // namespace Engine

Q_DECLARE_METATYPE(Engine::DnsServerScore)
Q_DECLARE_METATYPE(Engine::DnsBenchmarkResult)
