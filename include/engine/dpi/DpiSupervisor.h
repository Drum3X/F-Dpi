#pragma once

#include <atomic>

#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

#include "include/dpi/DpiLaunch.h"

class QProcess;
class QThread;
class QTimer;

namespace libcore {
    struct DpiStatus;
}

namespace Engine {
    // F-Dpi: runs zapret's engine for the running DPI Bypass profile. ConnectionManager decides when.
    // Windows (DpiSupervisor.cpp): winws as a QProcess on a thread of its own, so the blocking start and stop calls work from
    // the start/stop workers and from the main thread alike.
    // Linux (DpiSupervisorCore.cpp): nfqws needs root and an nftables rule, so the privileged core runs it (core/internal/dpi);
    // this class drives it over RPC and polls its output. It goes down with the core.
    class DpiSupervisor final : public QObject {
        Q_OBJECT

    public:
#ifdef Q_OS_LINUX
        static constexpr bool kRunsInCore = true;
#else
        static constexpr bool kRunsInCore = false;
#endif
        // How long winws must stay up after it started to count as running: it exits at once on a WinDivert or argument error.
        static constexpr int kStartupGraceMs = 1500;
        static constexpr int kStopWaitMs = 5000;

        // Main thread. Kills a winws left running by a crashed earlier run.
        // killStrays: kill winws processes of this installation that are already running (true for the profile engine; a
        // second supervisor, such as Blockcheck's, is created while the first one has nothing running).
        explicit DpiSupervisor(QObject *parent = nullptr, bool killStrays = true);
        ~DpiSupervisor() override;

        // Worker threads only, never the main thread: blocks for up to kStartupGraceMs. Stops a running winws first. False with
        // a translated error when winws did not start or exited within the grace period.
        bool start(const Dpi::Launch &launch, QString *error, int graceMs = kStartupGraceMs);

        // Any thread. Quiet: winws output and start/stop lines stay out of the log (kept for exitReason only).
        void setQuiet(bool quiet) { m_quiet = quiet; }

        // Any thread; blocks until winws is gone. A no-op when it is not running.
        void stop();

        // Any thread.
        bool running() const { return m_running.load(); }

    signals:
        // Main thread. winws exited on its own after a successful start(); reason is its last output or exit code.
        void unexpectedExit(const QString &reason);

    private:
        QString exitReason() const;
        QThread *m_thread = nullptr;

#ifdef Q_OS_LINUX
        // m_thread only.
        void poll();
        // Logs the new output lines and saves a changed autohostlist; false when nfqws is gone.
        bool takeStatus(const libcore::DpiStatus &status);
        void addLine(const QString &line);
        void writeAutoHostlist(const QByteArray &content);

        QTimer *m_poll = nullptr;
        qint64 m_lastSeq = 0;
        qint64 m_autoVersion = 0;
        QString m_autoHostlistPath; // the profile's autohostlist-<id>.txt, "" without one
        QString m_coreError;
#else
        // m_thread only.
        void stopOnThread();
        void readOutput();
        void onFinished(quint64 generation);

        // m_thread only.
        QProcess *m_process = nullptr;
        QByteArray m_partialLine;
        bool m_stopping = false;
#endif
        std::atomic<bool> m_quiet{false};

        std::atomic<bool> m_running{false};
        // Set once start() passed the grace period; an exit before that is reported by start() itself.
        std::atomic<bool> m_announceExit{false};
        std::atomic<quint64> m_generation{0};

        mutable QMutex m_tailMutex;
        QStringList m_tail;
        int m_exitCode = 0;
        bool m_crashed = false;
    };
} // namespace Engine
