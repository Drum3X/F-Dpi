#include "include/sys/NetworkMonitor.hpp"

#include <QFile>
#include <QTimer>

#include "include/global/Utils.hpp"

namespace Sys {
    namespace {
        constexpr int kNetworkMonitorDebounceMs = 1500;
        constexpr int kNetworkMonitorPollMs = 20000;

        NetworkIdentity networkMonitorOverride(const QString &path) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
            const auto parts = QString::fromUtf8(file.readAll()).trimmed().split('|');
            if (parts.size() < 3 || parts[1].isEmpty()) return {};
            NetworkIdentity network;
            network.kind = parts[0] == QLatin1String("wifi") ? NetworkIdentity::Kind::WiFi : NetworkIdentity::Kind::Ethernet;
            network.keys = parts[1].split(',', Qt::SkipEmptyParts);
            network.key = network.keys.value(0);
            network.name = parts[2];
            return network;
        }
    } // namespace

#ifndef Q_OS_WIN
    NetworkIdentity DetectNetwork() { return {}; }

    void *WatchNetworkChanges(std::function<void()>) { return nullptr; }

    void UnwatchNetworkChanges(void *) {}
#endif

    NetworkMonitor *NetworkMonitor::instance() {
        static auto *monitor = new NetworkMonitor;
        return monitor;
    }

    NetworkMonitor::~NetworkMonitor() {
        if (m_watch != nullptr) UnwatchNetworkChanges(m_watch);
    }

    void NetworkMonitor::start(bool detectSynchronously) {
        if (m_started) return;
        m_started = true;
        m_debounce = new QTimer(this);
        m_debounce->setSingleShot(true);
        m_debounce->setInterval(kNetworkMonitorDebounceMs);
        connect(m_debounce, &QTimer::timeout, this, [this] { detectNow(); });
        m_poll = new QTimer(this);
        m_poll->setInterval(kNetworkMonitorPollMs);
        connect(m_poll, &QTimer::timeout, this, [this] { detectNow(); });
        m_poll->start();
        m_watch = WatchNetworkChanges([this] { QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection); });
        if (detectSynchronously) {
            const QString override = qEnvironmentVariable("FDPI_NETWORK_OVERRIDE_FILE");
            m_detecting = true;
            onDetected(override.isEmpty() ? DetectNetwork() : networkMonitorOverride(override));
            return;
        }
        detectNow();
    }

    void NetworkMonitor::refresh() {
        if (m_debounce != nullptr) m_debounce->start();
    }

    void NetworkMonitor::detectNow() {
        if (m_detecting.exchange(true)) return;
        const QString override = qEnvironmentVariable("FDPI_NETWORK_OVERRIDE_FILE");
        runOnNewThread([this, override] {
            const auto network = override.isEmpty() ? DetectNetwork() : networkMonitorOverride(override);
            QMetaObject::invokeMethod(this, [this, network] { onDetected(network); }, Qt::QueuedConnection);
        });
    }

    void NetworkMonitor::onDetected(const NetworkIdentity &network) {
        m_detecting = false;
        if (m_detected && network == m_current) return;
        m_detected = true;
        m_current = network;
        emit networkChanged(network);
    }
} // namespace Sys
