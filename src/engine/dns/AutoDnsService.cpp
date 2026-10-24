#include "include/engine/dns/AutoDnsService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>
#include <QTimer>

#include "include/database/DatabaseManager.h"
#include "include/database/SettingsRepo.h"
#include "include/dns/AutoDns.h"
#include "include/dpi/DpiLaunch.h"
#include "include/engine/ThreadCheck.h"
#include "include/engine/connection/ConnectionManager.h"
#include "include/global/Utils.hpp"
#include "include/sys/NetworkMonitor.hpp"
#include "include/sys/SystemDns.hpp"

namespace Engine {
    namespace {
        constexpr qint64 kAutoDnsMaxAgeSecs = 7 * 24 * 3600;
        constexpr int kAutoDnsBootDelayMs = 8000;
        // After a network change: the new link needs a moment before DoH servers answer at their usual speed.
        constexpr int kAutoDnsNetworkDelayMs = 5000;

        QString autoDnsBackupPath() {
            return QDir(Dpi::ListDir()).filePath(QStringLiteral("dns-backup.json"));
        }

        QJsonObject autoDnsReadBackup() {
            QFile file(autoDnsBackupPath());
            if (!file.open(QIODevice::ReadOnly)) return {};
            return QJsonDocument::fromJson(file.readAll()).object();
        }

        bool autoDnsWriteBackup(const QJsonObject &backup) {
            QDir().mkpath(Dpi::ListDir());
            QSaveFile file(autoDnsBackupPath());
            if (!file.open(QIODevice::WriteOnly)) return false;
            file.write(QJsonDocument(backup).toJson());
            return file.commit();
        }
    } // namespace

    AutoDnsService::AutoDnsService(ConnectionManager *connection, QObject *parent)
        : QObject(parent), m_connection(connection), m_benchmark(new DnsBenchmark(this)) {
        connect(m_benchmark, &DnsBenchmark::finished, this, [this](const DnsBenchmarkResult &result) {
            if (result.cancelled) return;
            if (result.bestId.isEmpty()) {
                MW_show_log(QCoreApplication::translate("AutoDns", "[DNS] Automatic DNS: %1").arg(result.error));
                return;
            }
            adopt(result);
        });
    }

    void AutoDnsService::recoverAtBoot() {
        ENGINE_CHECK_MAIN_THREAD();
        if (!QFile::exists(autoDnsBackupPath())) return;
        QString error;
        if (restoreSystem(&error)) {
            MW_show_log(QCoreApplication::translate("AutoDns", "[DNS] Restored the Windows DNS settings F-Dpi had changed before it last closed."));
        } else {
            MW_show_log(QCoreApplication::translate("AutoDns", "[DNS] Could not restore the Windows DNS settings: %1").arg(error));
        }
    }

    void AutoDnsService::start() {
        ENGINE_CHECK_MAIN_THREAD();
        if (m_started) return;
        m_started = true;
        connect(Sys::NetworkMonitor::instance(), &Sys::NetworkMonitor::networkChanged, this, [this](const Sys::NetworkIdentity &network) {
            if (!network.connected()) return;
            // A newly connected adapter still resolves through the ISP.
            if (systemDnsActive()) {
                QString name;
                QStringList untouched;
                QString error;
                if (!applySystem(&name, &untouched, &error)) MW_show_log(QCoreApplication::translate("AutoDns", "[DNS] Could not set the Windows DNS: %1").arg(error));
            }
            QTimer::singleShot(kAutoDnsNetworkDelayMs, this, [this] { refreshIfStale(); });
        });
        QTimer::singleShot(kAutoDnsBootDelayMs, this, [this] { refreshIfStale(); });
    }

    void AutoDnsService::refreshIfStale() {
        ENGINE_CHECK_MAIN_THREAD();
        const auto &settings = Configs::dataManager->settingsRepo;
        if (settings->auto_dns_mode != Dns::kModeAuto || m_benchmark->running() || settings->prepare_exit) return;
        const auto network = Sys::NetworkMonitor::instance()->current();
        if (!network.connected()) return;
        // Through a tunnel the times would be the proxy's, not this network's.
        if (m_connection->activeEngine() == ActiveEngine::Proxy && m_connection->runningTun()) return;
        const qint64 checked = settings->auto_dns_checked.toLongLong();
        const bool fresh = Dns::FindProvider(settings->auto_dns_server) != nullptr && network.keys.contains(settings->auto_dns_network) &&
                           QDateTime::currentSecsSinceEpoch() - checked < kAutoDnsMaxAgeSecs;
        if (fresh) return;
        DnsBenchmarkOptions options;
        options.testDomains = Dns::DefaultTestDomains();
        options.poisonTest = false;
        m_benchmark->start(options);
    }

    void AutoDnsService::adopt(const DnsBenchmarkResult &result) {
        ENGINE_CHECK_MAIN_THREAD();
        const auto *provider = Dns::FindProvider(result.bestId);
        if (provider == nullptr) return;
        auto &settings = Configs::dataManager->settingsRepo;
        const bool switched = settings->auto_dns_server != provider->id;
        settings->auto_dns_server = provider->id;
        settings->auto_dns_latency = result.bestMs;
        settings->auto_dns_network = Sys::NetworkMonitor::instance()->current().key;
        settings->auto_dns_checked = QString::number(QDateTime::currentSecsSinceEpoch());
        settings->Save();
        if (settings->auto_dns_mode == Dns::kModeAuto) {
            MW_show_log(QCoreApplication::translate("AutoDns", "[DNS] Automatic DNS: %1, the fastest encrypted DNS here (%2 ms).")
                            .arg(provider->name)
                            .arg(result.bestMs));
            if (switched) settingsChanged();
        }
        emit changed();
    }

    void AutoDnsService::settingsChanged() {
        ENGINE_CHECK_MAIN_THREAD();
        const auto &settings = Configs::dataManager->settingsRepo;
        const auto engine = m_connection->activeEngine();
        if (engine == ActiveEngine::Dpi) {
            if (settings->auto_dns_system && Dns::ActiveProvider() != nullptr) dpiStarted();
            else dpiStopped();
        } else if (engine == ActiveEngine::Proxy && settings->auto_dns_proxy) {
            m_connection->noteRestartNeeded(RestartReason::Settings);
        }
        emit changed();
    }

    void AutoDnsService::dpiStarted() {
        const auto &settings = Configs::dataManager->settingsRepo;
        if (!settings->auto_dns_system || Dns::ActiveProvider() == nullptr) return;
        QString name;
        QStringList untouched;
        QString error;
        if (applySystem(&name, &untouched, &error)) {
            if (!untouched.isEmpty()) {
                MW_show_log(QCoreApplication::translate("AutoDns", "[DNS] Left alone, DNS servers are set for this network in Windows: %1")
                                .arg(untouched.join(QStringLiteral(", "))));
            }
            if (systemDnsActive()) {
                MW_show_log(QCoreApplication::translate("AutoDns", "[DNS] Windows now resolves through %1 over DNS-over-HTTPS while DPI Bypass runs.").arg(name));
            }
        } else {
            MW_show_log(QCoreApplication::translate("AutoDns", "[DNS] Could not set the Windows DNS: %1").arg(error));
        }
        QMetaObject::invokeMethod(this, [this] { emit changed(); }, Qt::QueuedConnection);
    }

    void AutoDnsService::dpiStopped() {
        if (!systemDnsActive()) return;
        QString error;
        if (restoreSystem(&error)) MW_show_log(QCoreApplication::translate("AutoDns", "[DNS] Windows DNS settings restored."));
        else MW_show_log(QCoreApplication::translate("AutoDns", "[DNS] Could not restore the Windows DNS settings: %1").arg(error));
        QMetaObject::invokeMethod(this, [this] { emit changed(); }, Qt::QueuedConnection);
    }

    bool AutoDnsService::applySystem(QString *providerName, QStringList *untouched, QString *error) {
        const auto *provider = Dns::ActiveProvider();
        if (provider == nullptr) return true;
        *providerName = provider->name;
        QMutexLocker lock(&m_mutex);
        auto backup = autoDnsReadBackup();
        if (!backup.contains(QStringLiteral("effective"))) {
            backup[QStringLiteral("effective")] = QJsonArray::fromStringList(Sys::SystemDns::EffectiveServers());
        }
        const bool ok = Sys::SystemDns::Apply({provider->ipv4, provider->ipv6, provider->dohTemplate}, &backup, untouched, error);
        backup[QStringLiteral("provider")] = provider->id;
        // Written even after a partial failure: what was changed must still be put back.
        if (!backup[QStringLiteral("adapters")].toObject().isEmpty() && !autoDnsWriteBackup(backup)) {
            QString ignored;
            Sys::SystemDns::Restore(backup, &ignored);
            *error = QCoreApplication::translate("AutoDns", "cannot save the backup of the current settings to %1").arg(autoDnsBackupPath());
            return false;
        }
        return ok;
    }

    bool AutoDnsService::restoreSystem(QString *error) {
        QMutexLocker lock(&m_mutex);
        return Dns::RestoreSystemDnsBackup(autoDnsBackupPath(), error);
    }

    bool AutoDnsService::systemDnsActive() const {
        QMutexLocker lock(&m_mutex);
        return QFile::exists(autoDnsBackupPath());
    }

    QStringList AutoDnsService::originalServers() const {
        QMutexLocker lock(&m_mutex);
        QStringList servers;
        for (const auto &value : autoDnsReadBackup()[QStringLiteral("effective")].toArray()) servers << value.toString();
        return servers;
    }
} // namespace Engine
