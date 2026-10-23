#pragma once

#include <atomic>

#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

#include "include/dpi/DpiLaunch.h"

class QProcess;
class QThread;

namespace Engine {
    // F-Dpi: runs zapret's winws for the running DPI Bypass profile. The QProcess lives on a thread of its own, so the blocking
    // start and stop calls work from the start/stop workers and from the main thread alike. ConnectionManager decides when.
    class DpiSupervisor final : public QObject {
        Q_OBJECT

    public:
        // How long winws must stay up after it started to count as running: it exits at once on a WinDivert or argument error.
        static constexpr int kStartupGraceMs = 1500;
        static constexpr int kStopWaitMs = 5000;

        // Main thread. Kills a winws left running by a crashed earlier run.
        explicit DpiSupervisor(QObject *parent = nullptr);
        ~DpiSupervisor() override;

        // Worker threads only, never the main thread: blocks for up to kStartupGraceMs. Stops a running winws first. False with
        // a translated error when winws did not start or exited within the grace period.
        bool start(const Dpi::Launch &launch, QString *error);

        // Any thread; blocks until winws is gone. A no-op when it is not running.
        void stop();

        // Any thread.
        bool running() const { return m_running.load(); }

    signals:
        // Main thread. winws exited on its own after a successful start(); reason is its last output or exit code.
        void unexpectedExit(const QString &reason);

    private:
        // m_thread only.
        void stopOnThread();
        void readOutput();
        void onFinished(quint64 generation);

        QString exitReason() const;

        QThread *m_thread = nullptr;
        // m_thread only.
        QProcess *m_process = nullptr;
        QByteArray m_partialLine;
        bool m_stopping = false;

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
