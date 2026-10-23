#pragma once

#include <atomic>

#include <QObject>
#include <QString>
#include <QStringList>

namespace Engine {
    class ConnectionManager;
    class DpiSupervisor;

    struct BlockcheckOptions {
        QStringList domains;
        // Quick: curated strategies, stops at the first that unblocks everything. Deep: every combination, keeps the best.
        bool deep = false;
        bool http = true;
        bool tls12 = true;
        bool tls13 = true;
        int timeoutMs = 3000;
    };

    struct BlockcheckResult {
        bool cancelled = false;
        // Set when the scan could not run at all.
        QString error;
        // Probes (domain × protocol) that failed without bypass, per target.
        int httpBlocked = 0;
        int tlsBlocked = 0;
        QString bestHttp;
        QString bestTls;
        int bestHttpPassed = 0;
        int bestTlsPassed = 0;
        // Ready for a dpi profile (Dpi::Blockcheck::ComposeStrategy); empty when nothing was blocked or nothing worked.
        QString strategy;
        QString wfTcp;
        QString wfUdp;

        [[nodiscard]] bool found() const { return !strategy.isEmpty(); }
    };

    // F-Dpi Blockcheck: finds a winws strategy that unblocks the given sites on this network.
    // 1. Resolves the sites over DNS-over-HTTPS (1.1.1.1), falling back to the system resolver, so a poisoned DNS does not
    //    fake a block. 2. Probes every site without bypass: HTTP on 80, TLS 1.2 and TLS 1.3 on 443 (QUIC cannot be probed).
    // 3. For each blocked target, runs winws with one strategy at a time and probes again. Needs administrator rights.
    // The running profile is stopped first and restarted at the end; no profile may start in between (exclusive task).
    class Blockcheck final : public QObject {
        Q_OBJECT

    public:
        explicit Blockcheck(ConnectionManager *connection, QObject *parent = nullptr);
        ~Blockcheck() override;

        // Main thread. False when already running.
        bool start(const BlockcheckOptions &options);

        // Any thread: stops after the current probe.
        void cancel();

        [[nodiscard]] bool running() const { return m_running; }

    signals:
        // All on the main thread.
        void message(const QString &line);
        void progress(int done, int total, const QString &current);
        // target: "HTTP" or "TLS".
        void strategyTested(const QString &target, const QString &strategy, int passed, int total);
        void finished(const Engine::BlockcheckResult &result);

    private:
        void run(const BlockcheckOptions &options);
        void finish(const BlockcheckResult &result);

        ConnectionManager *m_connection;
        DpiSupervisor *m_dpi = nullptr;
        bool m_running = false;
        int m_resumeProfile = -1;
        std::atomic<bool> m_cancel{false};
    };
} // namespace Engine

Q_DECLARE_METATYPE(Engine::BlockcheckResult)
