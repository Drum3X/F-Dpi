#pragma once

#include <atomic>
#include <functional>

#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

namespace Sys {
    // F-Dpi: the network the default route goes through.
    struct NetworkIdentity {
        enum class Kind { None, WiFi, Ethernet };
        Kind kind = Kind::None;
        // Every id the network goes by, most stable first: "nlm:{GUID}" (Windows network profile), "gw:<gateway MAC>",
        // "wifi:<SSID>" (only when Windows lets us read it: 24H2 asks for location access). A bound network matches on any.
        QStringList keys;
        // keys.first(); empty when offline.
        QString key;
        // For people: the SSID or Windows' network name, else the adapter and gateway address.
        QString name;
        // The adapter's addresses, for the network placeholders (include/sys/NetworkPlaceholders.h); empty when it has none.
        QString gateway;       // IPv4 default gateway
        QString gateway6;      // IPv6 default gateway
        QString localIp;       // this computer's IPv4 address on the adapter
        QString dhcpDns;       // the adapter's first DNS server (DHCP or set in Windows), IPv4 first
        QString interfaceName; // Windows' name for the adapter ("Wi-Fi")

        [[nodiscard]] bool connected() const { return !key.isEmpty(); }
        // A changed address counts as another network for networkChanged: a phone hotspot keeps its Windows network id but
        // hands out a new gateway on every connection. dhcpDns does not: F-Dpi itself rewrites it while DPI Bypass runs.
        bool operator==(const NetworkIdentity &other) const {
            return keys == other.keys && name == other.name && gateway == other.gateway && gateway6 == other.gateway6 &&
                   localIp == other.localIp && interfaceName == other.interfaceName;
        }
    };

    // Platform part (src/sys/windows/NetworkMonitor_win.cpp; a stub elsewhere). Any thread; may block for a few ms.
    NetworkIdentity DetectNetwork();

    // Calls onChange (any thread) whenever an interface or route changes; nullptr when the platform cannot tell.
    void *WatchNetworkChanges(std::function<void()> onChange);
    void UnwatchNetworkChanges(void *handle);

    // F-Dpi: reports network changes on the main thread. Detection runs off the UI thread after a short debounce, on every
    // platform change notice and on a slow poll as a fallback.
    // FDPI_NETWORK_OVERRIDE_FILE (tests): when set, the network is read from that file instead of detected:
    // "kind|key1,key2|name[|gateway|localIp|dhcpDns|interface|gateway6]", kind wifi or eth; the address fields are optional.
    class NetworkMonitor : public QObject {
        Q_OBJECT

    public:
        static NetworkMonitor *instance();

        // Main thread; idempotent. synchronous: the first detection runs right here, so current() is known on return.
        void start(bool synchronous = false);

        // Main thread. The last detected network.
        [[nodiscard]] NetworkIdentity current() const { return m_current; }

        // Any thread: the same, as of the last detection (config builds run on worker threads); detects right away while
        // the monitor has not run yet.
        static NetworkIdentity Latest();

        [[nodiscard]] bool detected() const { return m_detected; }

        // Main thread: detect again soon (debounced).
        void refresh();

    signals:
        // Main thread, once per change (the first detection included).
        void networkChanged(const Sys::NetworkIdentity &network);

    private:
        NetworkMonitor() = default;
        ~NetworkMonitor() override;

        void detectNow();
        void onDetected(const NetworkIdentity &network);

        QTimer *m_debounce = nullptr;
        QTimer *m_poll = nullptr;
        void *m_watch = nullptr;
        bool m_started = false;
        bool m_detected = false;
        std::atomic<bool> m_detecting{false};
        NetworkIdentity m_current;
    };
} // namespace Sys
