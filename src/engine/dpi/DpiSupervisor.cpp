#include "include/engine/dpi/DpiSupervisor.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QProcess>
#include <QThread>

#include "include/global/Logger.hpp"
#include "include/global/Utils.hpp"
#include "include/sys/ChildProcess.hpp"

namespace Engine {
    namespace {
        constexpr int kDpiSupervisorTailLines = 6;
        constexpr int kDpiSupervisorMaxLineChars = 400;
    } // namespace

    DpiSupervisor::DpiSupervisor(QObject *parent) : QObject(parent) {
        m_thread = new QThread;
        m_thread->setObjectName(QStringLiteral("DS_dpi"));
        m_thread->start();
        // A crashed run's winws outlives it only when the job object could not be set up; it would still bend traffic.
        if (const int killed = Sys::KillProcessesByPath(Dpi::WinwsPath()); killed > 0) {
            MW_show_log(QStringLiteral("[DPI] Stopped %1 winws process(es) left over from an earlier run.").arg(killed));
        }
    }

    DpiSupervisor::~DpiSupervisor() {
        stop();
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
    }

    bool DpiSupervisor::start(const Dpi::Launch &launch, QString *error) {
        Q_ASSERT(QThread::currentThread() != QCoreApplication::instance()->thread());
        QString startError;
        runOnThread([&] {
            stopOnThread();
            {
                QMutexLocker locker(&m_tailMutex);
                m_tail.clear();
                m_exitCode = 0;
                m_crashed = false;
            }
            m_partialLine.clear();
            m_announceExit = false;
            const quint64 generation = ++m_generation;

            m_process = new QProcess;
            m_process->setProgram(launch.program);
#ifdef Q_OS_WIN
            m_process->setNativeArguments(Dpi::WindowsCommandLine(launch.arguments));
#else
            m_process->setArguments(launch.arguments);
#endif
            m_process->setWorkingDirectory(launch.workingDir);
            m_process->setProcessChannelMode(QProcess::MergedChannels);
            connect(m_process, &QProcess::readyReadStandardOutput, m_process, [this] { readOutput(); });
            connect(m_process, &QProcess::finished, m_process, [this, generation](int exitCode, QProcess::ExitStatus status) {
                {
                    QMutexLocker locker(&m_tailMutex);
                    m_exitCode = exitCode;
                    m_crashed = status == QProcess::CrashExit;
                }
                onFinished(generation);
            });

            MW_show_log(QStringLiteral("[DPI] %1 %2").arg(QDir::toNativeSeparators(launch.program), Dpi::WindowsCommandLine(launch.arguments)));
            m_process->start();
            if (!m_process->waitForStarted(5000)) {
                startError = QCoreApplication::translate("Dpi", "winws could not be started: %1").arg(m_process->errorString());
                delete m_process;
                m_process = nullptr;
                return;
            }
            QString jobError;
            if (!Sys::TieChildToSelf(m_process->processId(), &jobError)) {
                LOG_WARN(QStringLiteral("[DPI] winws is not tied to F-Dpi's lifetime (%1); a crash may leave it running").arg(jobError));
            }
            m_running = true;
        }, m_thread, true);

        if (!startError.isEmpty()) {
            *error = startError;
            return false;
        }

        QElapsedTimer grace;
        grace.start();
        while (m_running.load() && grace.elapsed() < kStartupGraceMs) QThread::msleep(50);
        // Announce first: an exit right after the check below is then reported by the finished handler.
        m_announceExit = true;
        if (!m_running.load()) {
            m_announceExit = false;
            *error = QCoreApplication::translate("Dpi", "winws stopped right after it started.\n\n%1").arg(exitReason());
            return false;
        }
        return true;
    }

    void DpiSupervisor::stop() {
        if (!m_running.load()) {
            // Also reaps a process object whose exit was already reported.
            runOnThread([this] { stopOnThread(); }, m_thread, true);
            return;
        }
        runOnThread([this] { stopOnThread(); }, m_thread, true);
        MW_show_log(QStringLiteral("[DPI] winws stopped."));
    }

    void DpiSupervisor::stopOnThread() {
        if (m_process == nullptr) return;
        m_stopping = true;
        if (m_process->state() != QProcess::NotRunning) {
            // winws has no graceful stop over a pipe; the kernel releases its WinDivert handle when it dies.
            m_process->kill();
            m_process->waitForFinished(kStopWaitMs);
        }
        readOutput();
        delete m_process;
        m_process = nullptr;
        m_running = false;
        m_stopping = false;
    }

    void DpiSupervisor::readOutput() {
        if (m_process == nullptr) return;
        m_partialLine += m_process->readAllStandardOutput();
        qsizetype newline;
        while ((newline = m_partialLine.indexOf('\n')) >= 0) {
            QString line = QString::fromLocal8Bit(m_partialLine.left(newline)).trimmed();
            m_partialLine.remove(0, newline + 1);
            if (line.isEmpty()) continue;
            if (line.size() > kDpiSupervisorMaxLineChars) line = line.left(kDpiSupervisorMaxLineChars) + QStringLiteral("...");
            {
                QMutexLocker locker(&m_tailMutex);
                m_tail << line;
                while (m_tail.size() > kDpiSupervisorTailLines) m_tail.removeFirst();
            }
            MW_show_log(QStringLiteral("[DPI] ") + line);
        }
    }

    void DpiSupervisor::onFinished(quint64 generation) {
        if (m_partialLine.size() > 0) m_partialLine += '\n';
        readOutput();
        if (m_stopping || generation != m_generation.load()) return;
        m_running = false;
        if (!m_announceExit.exchange(false)) return;
        const QString reason = exitReason();
        MW_show_log(QStringLiteral("[DPI] winws exited unexpectedly: ") + reason);
        QMetaObject::invokeMethod(this, [this, reason] { emit unexpectedExit(reason); }, Qt::QueuedConnection);
    }

    QString DpiSupervisor::exitReason() const {
        QMutexLocker locker(&m_tailMutex);
        // The full output is already in the log; the last line says why winws stopped.
        const QString last = m_tail.isEmpty() ? QString() : m_tail.last();
        QString reason = m_crashed ? QCoreApplication::translate("Dpi", "it crashed") : QCoreApplication::translate("Dpi", "exit code %1").arg(m_exitCode);
        if (!last.isEmpty()) reason = QStringLiteral("%1 (%2)").arg(last, reason);
        if (m_crashed || !last.contains(QStringLiteral("windivert"), Qt::CaseInsensitive)) return reason;
        // winws exits with the Windows error code of a failed WinDivertOpen.
        QString hint;
        switch (m_exitCode) {
            case 5: hint = QCoreApplication::translate("Dpi", "WinDivert needs administrator rights: start F-Dpi as administrator."); break;
            case 2:
            case 3: hint = QCoreApplication::translate("Dpi", "WinDivert64.sys is missing from the zapret folder; reinstall F-Dpi."); break;
            case 87: hint = QCoreApplication::translate("Dpi", "WinDivert rejected the capture filter; check the TCP and UDP ports."); break;
            case 577:
            case 1275: hint = QCoreApplication::translate("Dpi", "Windows blocked the WinDivert driver (driver signature policy or antivirus)."); break;
            default: break;
        }
        return hint.isEmpty() ? reason : QStringLiteral("%1\n\n%2").arg(hint, reason);
    }
} // namespace Engine
