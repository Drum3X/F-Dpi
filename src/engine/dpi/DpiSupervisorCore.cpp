#include "include/engine/dpi/DpiSupervisor.h"

// Linux: nfqws runs in the privileged core (core/internal/dpi); this side starts and stops it over RPC, polls its output into
// the log and saves the autohostlist it learns (the core never writes the user's files). Windows: DpiSupervisor.cpp.
#ifdef Q_OS_LINUX
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSaveFile>
#include <QThread>
#include <QTimer>

#include "include/api/RPC.h"
#include "include/global/Logger.hpp"
#include "include/global/Utils.hpp"

namespace Engine {
    namespace {
        constexpr int kDpiCoreTailLines = 6;
        constexpr int kDpiCoreMaxLineChars = 400;
        constexpr int kDpiCorePollMs = 300;

        bool dpiCoreConnected() {
            return API::defaultClient != nullptr && API::defaultClient->IsConnected();
        }
    } // namespace

    DpiSupervisor::DpiSupervisor(QObject *parent, bool killStrays) : QObject(parent) {
        // The core kills an nfqws an earlier core left behind when it starts (dpi.CleanupAtBoot).
        Q_UNUSED(killStrays);
        m_thread = new QThread;
        m_thread->setObjectName(QStringLiteral("DS_dpi"));
        m_thread->start();
        runOnThread([this] {
            m_poll = new QTimer;
            m_poll->setInterval(kDpiCorePollMs);
            connect(m_poll, &QTimer::timeout, m_poll, [this] { poll(); });
        }, m_thread, true);
    }

    DpiSupervisor::~DpiSupervisor() {
        stop();
        runOnThread([this] {
            delete m_poll;
            m_poll = nullptr;
        }, m_thread, true);
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
    }

    bool DpiSupervisor::start(const Dpi::Launch &launch, QString *error, int graceMs) {
        Q_ASSERT(QThread::currentThread() != QCoreApplication::instance()->thread());
        // Stops the poll first: a stale poll must not report the previous nfqws' exit for this start.
        runOnThread([this] { m_poll->stop(); }, m_thread, true);
        m_announceExit = false;
        m_running = false;
        ++m_generation;
        {
            QMutexLocker locker(&m_tailMutex);
            m_tail.clear();
            m_exitCode = 0;
            m_crashed = false;
        }
        m_coreError.clear();
        m_autoVersion = 0;
        m_autoHostlistPath.clear();
        for (const auto &argument : launch.arguments) {
            if (argument.startsWith(QStringLiteral("--hostlist-auto="))) m_autoHostlistPath = argument.section('=', 1);
        }

        if (!dpiCoreConnected()) {
            *error = QCoreApplication::translate("Dpi", "The core is not running, so DPI Bypass cannot start.");
            return false;
        }
        bool ok = false;
        // Lines before this start are not this nfqws'.
        const auto before = API::defaultClient->DpiQuery(&ok, 0, 0);
        m_lastSeq = ok ? before.last_seq.value() : 0;

        if (!m_quiet) MW_show_log(QStringLiteral("[DPI] ") + Dpi::DisplayCommandLine(launch));
        const QString coreError = API::defaultClient->DpiStart(&ok, launch.arguments, launch.tcpPorts, launch.udpPorts, graceMs);
        // The output of a failed start explains it; read it before reporting.
        if (const auto status = API::defaultClient->DpiQuery(&ok, m_lastSeq, 0); ok) takeStatus(status);
        if (!coreError.isEmpty()) {
            m_coreError = coreError;
            *error = QCoreApplication::translate("Dpi", "nfqws did not start.\n\n%1").arg(exitReason());
            return false;
        }
        m_running = true;
        m_announceExit = true;
        runOnThread([this] { m_poll->start(); }, m_thread, true);
        return true;
    }

    void DpiSupervisor::stop() {
        runOnThread([this] { m_poll->stop(); }, m_thread, true);
        m_announceExit = false;
        const bool wasRunning = m_running.exchange(false);
        // A dead core took nfqws with it; the owner table of its rules went with its socket.
        if (!dpiCoreConnected()) return;
        bool ok = false;
        const auto status = API::defaultClient->DpiStop(&ok);
        if (ok) takeStatus(status);
        if (wasRunning && !m_quiet) MW_show_log(QStringLiteral("[DPI] nfqws stopped."));
    }

    void DpiSupervisor::poll() {
        if (!m_running.load()) return;
        const quint64 generation = m_generation.load();
        bool ok = false;
        libcore::DpiStatus status;
        if (dpiCoreConnected()) status = API::defaultClient->DpiQuery(&ok, m_lastSeq, m_autoVersion);
        if (generation != m_generation.load() || !m_running.load()) return;
        bool alive = ok && takeStatus(status);
        if (!ok) m_coreError = QCoreApplication::translate("Dpi", "the core stopped");
        if (alive) return;
        m_poll->stop();
        m_running = false;
        if (!m_announceExit.exchange(false)) return;
        const QString reason = exitReason();
        MW_show_log(QStringLiteral("[DPI] nfqws exited unexpectedly: ") + reason);
        QMetaObject::invokeMethod(this, [this, reason] { emit unexpectedExit(reason); }, Qt::QueuedConnection);
    }

    bool DpiSupervisor::takeStatus(const libcore::DpiStatus &status) {
        for (const auto &line : status.lines) addLine(QString::fromStdString(line));
        if (status.last_seq.value() > m_lastSeq) m_lastSeq = status.last_seq.value();
        if (status.autohostlist_changed.value()) {
            m_autoVersion = status.autohostlist_version.value();
            writeAutoHostlist(QByteArray::fromStdString(status.autohostlist.value()));
        }
        if (status.exited.value()) {
            QMutexLocker locker(&m_tailMutex);
            m_exitCode = status.exit_code.value();
            m_crashed = m_exitCode < 0;
        }
        return status.running.value();
    }

    void DpiSupervisor::addLine(const QString &raw) {
        QString line = raw.trimmed();
        if (line.isEmpty()) return;
        if (line.size() > kDpiCoreMaxLineChars) line = line.left(kDpiCoreMaxLineChars) + QStringLiteral("...");
        {
            QMutexLocker locker(&m_tailMutex);
            m_tail << line;
            while (m_tail.size() > kDpiCoreTailLines) m_tail.removeFirst();
        }
        if (!m_quiet) MW_show_log(QStringLiteral("[DPI] ") + line);
    }

    // The core reads nfqws' copy in its runtime folder and hands the content over; the file stays the user's.
    void DpiSupervisor::writeAutoHostlist(const QByteArray &content) {
        if (m_autoHostlistPath.isEmpty()) return;
        QSaveFile file(m_autoHostlistPath);
        if (file.open(QIODevice::WriteOnly) && file.write(content) == content.size() && file.commit()) return;
        LOG_WARN(QStringLiteral("[DPI] Cannot save the learned sites to %1: %2").arg(m_autoHostlistPath, file.errorString()));
    }

    QString DpiSupervisor::exitReason() const {
        QMutexLocker locker(&m_tailMutex);
        const QString last = m_tail.isEmpty() ? QString() : m_tail.last();
        // The core's error already carries nfqws' last line and exit code.
        QString reason = m_coreError;
        if (reason.isEmpty()) {
            reason = m_crashed ? QCoreApplication::translate("Dpi", "it crashed") : QCoreApplication::translate("Dpi", "exit code %1").arg(m_exitCode);
            if (!last.isEmpty()) reason = QStringLiteral("%1 (%2)").arg(last, reason);
        }
        // The core's "the core has no root privileges ..." (dpi.Start), not its trust errors that also name root.
        if (m_coreError.contains(QStringLiteral("has no root privileges"))) {
            reason = QCoreApplication::translate("Dpi", "The core needs root privileges for DPI Bypass: give the core root privileges, then start again.")
                     + QStringLiteral("\n\n") + reason;
        } else if (last.contains(QStringLiteral("nfq_"), Qt::CaseInsensitive) || last.contains(QStringLiteral("queue"), Qt::CaseInsensitive)) {
            reason = QCoreApplication::translate("Dpi", "nfqws could not read the packet queue: another program (such as zapret's own service) may be using queue 200.")
                     + QStringLiteral("\n\n") + reason;
        }
        return reason;
    }
} // namespace Engine
#endif // Q_OS_LINUX
