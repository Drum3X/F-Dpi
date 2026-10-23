#pragma once

#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

#include "include/engine/dns/DnsBenchmark.h"

namespace Engine {
    class ConnectionManager;

    // F-Dpi Otomatik DNS at run time (include/dns/AutoDns.h has the settings and providers).
    // - Keeps the pick fresh: with mode "auto" it benchmarks in the background at boot and on a network change when the
    //   last pick was made on another network, is older than a week, or there is none.
    // - DPI Bypass: while winws runs, the Windows adapters resolve through the active provider with DoH required (the
    //   proxy engine, which would otherwise resolve, is idle). The originals are saved to <config>/dpi/dns-backup.json
    //   first and put back when DPI stops; a backup left by a crash is restored at the next boot.
    // - Proxy profiles pick the provider up when their config is built (generate.cpp); a change notes a restart.
    class AutoDnsService final : public QObject {
        Q_OBJECT

    public:
        explicit AutoDnsService(ConnectionManager *connection, QObject *parent = nullptr);

        // Main thread, before any profile starts: restores adapters a crashed run left on F-Dpi's DNS.
        void recoverAtBoot();

        // Main thread, after the network monitor started: follows network changes, schedules the boot refresh.
        void start();

        // Any thread (ConnectionManager's start/stop workers). Applied only with auto_dns_system and a provider.
        void dpiStarted();
        void dpiStopped();

        // Main thread, after the Auto DNS settings changed: applies or restores the system DNS for a running DPI profile,
        // and notes a restart for a running proxy profile.
        void settingsChanged();

        // Main thread: takes a benchmark's pick (also from the Auto DNS dialog) as the auto choice for this network.
        void adopt(const DnsBenchmarkResult &result);

        // Any thread. Windows' DNS is F-Dpi's right now.
        bool systemDnsActive() const;

        // Any thread. The servers the system resolved with before F-Dpi changed them; empty when it has not.
        QStringList originalServers() const;

        [[nodiscard]] bool refreshing() const { return m_benchmark->running(); }

    signals:
        // Main thread: the pick or the system DNS state changed.
        void changed();

    private:
        void refreshIfStale();
        // untouched: adapters left alone because their network has DNS set in Windows (Sys::SystemDns::Apply).
        bool applySystem(QString *providerName, QStringList *untouched, QString *error);
        bool restoreSystem(QString *error);

        ConnectionManager *m_connection;
        DnsBenchmark *m_benchmark;
        mutable QMutex m_mutex;
        bool m_started = false;
    };
} // namespace Engine
