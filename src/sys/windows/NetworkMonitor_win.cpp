#include "include/sys/NetworkMonitor.hpp"

#include <QString>

#include <memory>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <netlistmgr.h>
#include <wlanapi.h>

namespace Sys {
    namespace {
        // Only physical Wi-Fi and Ethernet: Wintun and other tunnels report other interface types.
        bool networkMonitorPhysical(const IP_ADAPTER_ADDRESSES *adapter) {
            return adapter->IfType == IF_TYPE_IEEE80211 || adapter->IfType == IF_TYPE_ETHERNET_CSMACD;
        }

        const SOCKADDR *networkMonitorIpv4Gateway(const IP_ADAPTER_ADDRESSES *adapter) {
            for (auto *gateway = adapter->FirstGatewayAddress; gateway != nullptr; gateway = gateway->Next) {
                if (gateway->Address.lpSockaddr != nullptr && gateway->Address.lpSockaddr->sa_family == AF_INET) {
                    return gateway->Address.lpSockaddr;
                }
            }
            return nullptr;
        }

        QString networkMonitorGuid(const GUID &guid) {
            wchar_t text[64];
            StringFromGUID2(guid, text, 64);
            return QString::fromWCharArray(text).toLower();
        }

        // Windows 11 24H2 refuses the SSID to apps without location access; empty then.
        QString networkMonitorSsid(const QString &adapterGuid) {
            HANDLE client = nullptr;
            DWORD version = 0;
            if (WlanOpenHandle(2, nullptr, &version, &client) != ERROR_SUCCESS) return {};
            QString ssid;
            PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
            if (WlanEnumInterfaces(client, nullptr, &interfaces) == ERROR_SUCCESS) {
                for (DWORD i = 0; i < interfaces->dwNumberOfItems && ssid.isEmpty(); ++i) {
                    const auto &info = interfaces->InterfaceInfo[i];
                    if (networkMonitorGuid(info.InterfaceGuid) != adapterGuid) continue;
                    PWLAN_CONNECTION_ATTRIBUTES attributes = nullptr;
                    DWORD size = 0;
                    if (WlanQueryInterface(client, &info.InterfaceGuid, wlan_intf_opcode_current_connection, nullptr, &size,
                                           reinterpret_cast<PVOID *>(&attributes), nullptr) == ERROR_SUCCESS) {
                        const auto &dot11 = attributes->wlanAssociationAttributes.dot11Ssid;
                        ssid = QString::fromUtf8(reinterpret_cast<const char *>(dot11.ucSSID), static_cast<int>(dot11.uSSIDLength));
                        WlanFreeMemory(attributes);
                    }
                }
                WlanFreeMemory(interfaces);
            }
            WlanCloseHandle(client, nullptr);
            return ssid;
        }

        // Windows' own network profile for the adapter (Network List Manager): its id and the name the user sees.
        bool networkMonitorNlm(const QString &adapterGuid, QString *networkId, QString *name) {
            const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            bool found = false;
            INetworkListManager *manager = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL, IID_INetworkListManager,
                                           reinterpret_cast<void **>(&manager)))) {
                IEnumNetworkConnections *connections = nullptr;
                if (SUCCEEDED(manager->GetNetworkConnections(&connections))) {
                    INetworkConnection *connection = nullptr;
                    while (!found && connections->Next(1, &connection, nullptr) == S_OK) {
                        GUID adapterId{};
                        INetwork *network = nullptr;
                        if (SUCCEEDED(connection->GetAdapterId(&adapterId)) && networkMonitorGuid(adapterId) == adapterGuid &&
                            SUCCEEDED(connection->GetNetwork(&network))) {
                            GUID id{};
                            BSTR text = nullptr;
                            if (SUCCEEDED(network->GetNetworkId(&id))) *networkId = networkMonitorGuid(id);
                            if (SUCCEEDED(network->GetName(&text)) && text != nullptr) {
                                *name = QString::fromWCharArray(text);
                                SysFreeString(text);
                            }
                            network->Release();
                            found = !networkId->isEmpty();
                        }
                        connection->Release();
                    }
                    connections->Release();
                }
                manager->Release();
            }
            if (SUCCEEDED(init)) CoUninitialize();
            return found;
        }

        QString networkMonitorGatewayMac(const IP_ADAPTER_ADDRESSES *adapter, const SOCKADDR *gateway) {
            MIB_IPNET_ROW2 row{};
            row.InterfaceIndex = adapter->IfIndex;
            row.Address.Ipv4 = *reinterpret_cast<const SOCKADDR_IN *>(gateway);
            if (GetIpNetEntry2(&row) != NO_ERROR || row.PhysicalAddressLength == 0) {
                if (ResolveIpNetEntry2(&row, nullptr) != NO_ERROR) return {};
            }
            QStringList bytes;
            for (ULONG i = 0; i < row.PhysicalAddressLength; ++i) bytes << QStringLiteral("%1").arg(row.PhysicalAddress[i], 2, 16, QLatin1Char('0'));
            return bytes.join('-');
        }

        QString networkMonitorAddress(const SOCKADDR *address) {
            char text[INET6_ADDRSTRLEN] = {};
            if (address->sa_family == AF_INET6) {
                inet_ntop(AF_INET6, &reinterpret_cast<const SOCKADDR_IN6 *>(address)->sin6_addr, text, sizeof(text));
            } else {
                inet_ntop(AF_INET, &reinterpret_cast<const SOCKADDR_IN *>(address)->sin_addr, text, sizeof(text));
            }
            return QString::fromLatin1(text);
        }

        // The IPv6 default gateway; usually link-local (fe80::...).
        QString networkMonitorIpv6Gateway(const IP_ADAPTER_ADDRESSES *adapter) {
            for (auto *gateway = adapter->FirstGatewayAddress; gateway != nullptr; gateway = gateway->Next) {
                if (gateway->Address.lpSockaddr != nullptr && gateway->Address.lpSockaddr->sa_family == AF_INET6) {
                    return networkMonitorAddress(gateway->Address.lpSockaddr);
                }
            }
            return {};
        }

        // This computer's IPv4 address on the adapter; an APIPA (169.254.x.x) address only when it has nothing else.
        QString networkMonitorLocalIpv4(const IP_ADAPTER_ADDRESSES *adapter) {
            QString fallback;
            for (auto *unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
                const auto *address = unicast->Address.lpSockaddr;
                if (address == nullptr || address->sa_family != AF_INET) continue;
                const auto text = networkMonitorAddress(address);
                if (!text.startsWith(QStringLiteral("169.254."))) return text;
                if (fallback.isEmpty()) fallback = text;
            }
            return fallback;
        }

        QString networkMonitorFirstDns(const IP_ADAPTER_ADDRESSES *adapter) {
            QString ipv6;
            for (auto *dns = adapter->FirstDnsServerAddress; dns != nullptr; dns = dns->Next) {
                const auto *address = dns->Address.lpSockaddr;
                if (address == nullptr) continue;
                if (address->sa_family == AF_INET) return networkMonitorAddress(address);
                if (address->sa_family == AF_INET6 && ipv6.isEmpty()) ipv6 = networkMonitorAddress(address);
            }
            return ipv6;
        }

        void CALLBACK networkMonitorInterfaceChanged(PVOID context, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) {
            (*static_cast<std::function<void()> *>(context))();
        }

        struct NetworkMonitorWatch {
            std::function<void()> onChange;
            HANDLE interfaceHandle = nullptr;
        };
    } // namespace

    NetworkIdentity DetectNetwork() {
        ULONG size = 16 * 1024;
        std::vector<unsigned char> buffer;
        ULONG result = ERROR_BUFFER_OVERFLOW;
        const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST;
        for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
            buffer.resize(size);
            result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &size);
        }
        if (result != NO_ERROR) return {};

        // The adapter Windows routes through: the lowest IPv4 metric among the physical ones with a gateway.
        const IP_ADAPTER_ADDRESSES *best = nullptr;
        for (auto *adapter = reinterpret_cast<const IP_ADAPTER_ADDRESSES *>(buffer.data()); adapter != nullptr; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp || !networkMonitorPhysical(adapter)) continue;
            if (networkMonitorIpv4Gateway(adapter) == nullptr) continue;
            if (best == nullptr || adapter->Ipv4Metric < best->Ipv4Metric) best = adapter;
        }
        if (best == nullptr) return {};

        const auto adapterGuid = QString::fromLatin1(best->AdapterName).toLower();
        const auto *gateway = networkMonitorIpv4Gateway(best);
        const bool wifi = best->IfType == IF_TYPE_IEEE80211;

        NetworkIdentity network;
        network.kind = wifi ? NetworkIdentity::Kind::WiFi : NetworkIdentity::Kind::Ethernet;
        QString nlmId;
        QString nlmName;
        if (networkMonitorNlm(adapterGuid, &nlmId, &nlmName)) network.keys << QStringLiteral("nlm:") + nlmId;
        if (const auto mac = networkMonitorGatewayMac(best, gateway); !mac.isEmpty()) network.keys << QStringLiteral("gw:") + mac;
        const auto ssid = wifi ? networkMonitorSsid(adapterGuid) : QString();
        if (!ssid.isEmpty()) network.keys << QStringLiteral("wifi:") + ssid;
        const auto gatewayAddress = networkMonitorAddress(gateway);
        if (network.keys.isEmpty()) network.keys << QStringLiteral("gwip:") + gatewayAddress;
        network.key = network.keys.first();
        if (!ssid.isEmpty()) network.name = ssid;
        else if (!nlmName.isEmpty()) network.name = nlmName;
        else network.name = QStringLiteral("%1 (%2)").arg(QString::fromWCharArray(best->FriendlyName), gatewayAddress);
        network.gateway = gatewayAddress;
        network.gateway6 = networkMonitorIpv6Gateway(best);
        network.localIp = networkMonitorLocalIpv4(best);
        network.dhcpDns = networkMonitorFirstDns(best);
        network.interfaceName = QString::fromWCharArray(best->FriendlyName);
        return network;
    }

    void *WatchNetworkChanges(std::function<void()> onChange) {
        auto watch = std::make_unique<NetworkMonitorWatch>();
        watch->onChange = std::move(onChange);
        if (NotifyIpInterfaceChange(AF_UNSPEC, networkMonitorInterfaceChanged, &watch->onChange, FALSE, &watch->interfaceHandle) != NO_ERROR) {
            return nullptr;
        }
        return watch.release();
    }

    void UnwatchNetworkChanges(void *handle) {
        auto *watch = static_cast<NetworkMonitorWatch *>(handle);
        if (watch == nullptr) return;
        if (watch->interfaceHandle != nullptr) CancelMibChangeNotify2(watch->interfaceHandle);
        delete watch;
    }
} // namespace Sys
