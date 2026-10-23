#include "include/sys/SystemDns.hpp"

#include <QHostAddress>
#include <QJsonArray>
#include <QRegularExpression>

#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <objbase.h>

namespace Sys::SystemDns {
    namespace {
        // Windows 10 2004+ (DoH properties: Windows 11). Looked up at run time so F-Dpi still starts where they are missing.
        using SystemDnsGetFn = DWORD(WINAPI *)(GUID, DNS_INTERFACE_SETTINGS *);
        using SystemDnsSetFn = DWORD(WINAPI *)(GUID, const DNS_INTERFACE_SETTINGS *);
        using SystemDnsFreeFn = VOID(WINAPI *)(DNS_INTERFACE_SETTINGS *);

        struct SystemDnsApi {
            SystemDnsGetFn get = nullptr;
            SystemDnsSetFn set = nullptr;
            SystemDnsFreeFn free = nullptr;
            [[nodiscard]] bool ok() const { return get != nullptr && set != nullptr && free != nullptr; }
        };

        const SystemDnsApi &systemDnsApi() {
            static const SystemDnsApi api = [] {
                SystemDnsApi loaded;
                if (const HMODULE module = LoadLibraryW(L"iphlpapi.dll")) {
                    loaded.get = reinterpret_cast<SystemDnsGetFn>(GetProcAddress(module, "GetInterfaceDnsSettings"));
                    loaded.set = reinterpret_cast<SystemDnsSetFn>(GetProcAddress(module, "SetInterfaceDnsSettings"));
                    loaded.free = reinterpret_cast<SystemDnsFreeFn>(GetProcAddress(module, "FreeInterfaceDnsSettings"));
                }
                return loaded;
            }();
            return api;
        }

        struct SystemDnsAdapter {
            QString guid;
            QString name;
            QStringList servers;
        };

        // Connected Wi-Fi and Ethernet adapters with a gateway: the ones traffic and DNS queries leave through.
        std::vector<SystemDnsAdapter> systemDnsAdapters() {
            ULONG size = 32 * 1024;
            std::vector<unsigned char> buffer;
            ULONG result = ERROR_BUFFER_OVERFLOW;
            for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
                buffer.resize(size);
                result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr,
                                              reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &size);
            }
            std::vector<SystemDnsAdapter> adapters;
            if (result != NO_ERROR) return adapters;
            for (auto *adapter = reinterpret_cast<const IP_ADAPTER_ADDRESSES *>(buffer.data()); adapter != nullptr; adapter = adapter->Next) {
                if (adapter->OperStatus != IfOperStatusUp) continue;
                if (adapter->IfType != IF_TYPE_IEEE80211 && adapter->IfType != IF_TYPE_ETHERNET_CSMACD) continue;
                if (adapter->FirstGatewayAddress == nullptr) continue;
                SystemDnsAdapter item;
                item.guid = QString::fromLatin1(adapter->AdapterName);
                item.name = QString::fromWCharArray(adapter->FriendlyName);
                QStringList v6;
                for (auto *dns = adapter->FirstDnsServerAddress; dns != nullptr; dns = dns->Next) {
                    // Not WSAAddressToString: it needs WSAStartup, which may not have run yet this early.
                    const auto address = QHostAddress(dns->Address.lpSockaddr).toString().section('%', 0, 0);
                    if (address.isEmpty()) continue;
                    // Windows lists these placeholders when an adapter has no IPv6 DNS server.
                    if (address.startsWith(QStringLiteral("fec0:"), Qt::CaseInsensitive)) continue;
                    if (dns->Address.lpSockaddr->sa_family == AF_INET6) v6 << address;
                    else item.servers << address;
                }
                item.servers << v6;
                adapters.push_back(item);
            }
            return adapters;
        }

        bool systemDnsGuid(const QString &text, GUID *guid) {
            return SUCCEEDED(CLSIDFromString(reinterpret_cast<LPCOLESTR>(text.utf16()), guid));
        }

        QString systemDnsWin32Error(DWORD code) {
            wchar_t *text = nullptr;
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                           reinterpret_cast<LPWSTR>(&text), 0, nullptr);
            QString message = text != nullptr ? QString::fromWCharArray(text).trimmed() : QString();
            if (text != nullptr) LocalFree(text);
            return QStringLiteral("%1 (%2)").arg(message.isEmpty() ? QStringLiteral("error") : message).arg(code);
        }

        // "a,b c" -> [a, b, c]
        QStringList systemDnsSplit(const QString &servers) {
            return servers.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
        }

        // One address family of an adapter: its static servers ("" = from DHCP) and their DoH settings, and the servers
        // Windows 11 keeps for the connected network's profile (Settings > Wi-Fi > DNS server assignment).
        QJsonObject systemDnsRead(const GUID &guid, bool ipv6, QString *error) {
            const auto &api = systemDnsApi();
            DNS_INTERFACE_SETTINGS3 settings{};
            settings.Version = DNS_INTERFACE_SETTINGS_VERSION3;
            settings.Flags = ipv6 ? DNS_SETTING_IPV6 : 0;
            const DWORD code = api.get(guid, reinterpret_cast<DNS_INTERFACE_SETTINGS *>(&settings));
            if (code != NO_ERROR) {
                *error = systemDnsWin32Error(code);
                return {};
            }
            QJsonObject family;
            family[QStringLiteral("servers")] = settings.NameServer != nullptr ? QString::fromWCharArray(settings.NameServer) : QString();
            QJsonArray doh;
            for (ULONG i = 0; i < settings.cServerProperties; ++i) {
                const auto &property = settings.ServerProperties[i];
                if (property.Type != DnsServerDohProperty || property.Property.DohSettings == nullptr) continue;
                const auto *server = property.Property.DohSettings;
                doh.append(QJsonObject{
                    {QStringLiteral("index"), static_cast<int>(property.ServerIndex)},
                    {QStringLiteral("template"), server->Template != nullptr ? QString::fromWCharArray(server->Template) : QString()},
                    {QStringLiteral("flags"), static_cast<qint64>(server->Flags)},
                });
            }
            family[QStringLiteral("doh")] = doh;
            family[QStringLiteral("profile_servers")] =
                settings.ProfileNameServer != nullptr ? QString::fromWCharArray(settings.ProfileNameServer) : QString();
            api.free(reinterpret_cast<DNS_INTERFACE_SETTINGS *>(&settings));
            return family;
        }

        struct SystemDnsDoh {
            ULONG index;
            std::wstring templ;
            ULONG64 flags;
        };

        bool systemDnsWrite(const GUID &guid, bool ipv6, const QString &servers, const std::vector<SystemDnsDoh> &doh, QString *error) {
            std::wstring nameServer = servers.toStdWString();
            std::vector<DNS_DOH_SERVER_SETTINGS> settingsList(doh.size());
            std::vector<DNS_SERVER_PROPERTY> properties(doh.size());
            for (size_t i = 0; i < doh.size(); ++i) {
                settingsList[i].Template = const_cast<PWSTR>(doh[i].templ.c_str());
                settingsList[i].Flags = doh[i].flags;
                properties[i].Version = DNS_SERVER_PROPERTY_VERSION1;
                properties[i].ServerIndex = doh[i].index;
                properties[i].Type = DnsServerDohProperty;
                properties[i].Property.DohSettings = &settingsList[i];
            }
            DNS_INTERFACE_SETTINGS3 settings{};
            settings.Version = DNS_INTERFACE_SETTINGS_VERSION3;
            settings.Flags = DNS_SETTING_NAMESERVER | DNS_SETTING_DOH | (ipv6 ? DNS_SETTING_IPV6 : 0);
            settings.NameServer = nameServer.data();
            settings.cServerProperties = static_cast<ULONG>(properties.size());
            settings.ServerProperties = properties.empty() ? nullptr : properties.data();
            const DWORD code = systemDnsApi().set(guid, reinterpret_cast<const DNS_INTERFACE_SETTINGS *>(&settings));
            if (code != NO_ERROR) {
                *error = systemDnsWin32Error(code);
                return false;
            }
            return true;
        }

        std::vector<SystemDnsDoh> systemDnsDohFor(const QStringList &servers, const QString &dohTemplate) {
            std::vector<SystemDnsDoh> doh;
            for (int i = 0; i < servers.size(); ++i) doh.push_back({static_cast<ULONG>(i), dohTemplate.toStdWString(), DNS_DOH_SERVER_SETTINGS_ENABLE});
            return doh;
        }

        std::vector<SystemDnsDoh> systemDnsDohFrom(const QJsonArray &saved) {
            std::vector<SystemDnsDoh> doh;
            for (const auto &value : saved) {
                const auto object = value.toObject();
                doh.push_back({static_cast<ULONG>(object[QStringLiteral("index")].toInt()), object[QStringLiteral("template")].toString().toStdWString(),
                               static_cast<ULONG64>(object[QStringLiteral("flags")].toInteger())});
            }
            return doh;
        }
    } // namespace

    bool Supported() {
        return systemDnsApi().ok();
    }

    bool Apply(const Servers &servers, QJsonObject *backup, QStringList *untouched, QString *error) {
        if (!Supported()) {
            *error = QStringLiteral("this Windows version cannot set DNS-over-HTTPS per adapter (Windows 11 needed)");
            return false;
        }
        const auto adapters = systemDnsAdapters();
        if (adapters.empty()) {
            *error = QStringLiteral("no connected network adapter");
            return false;
        }
        auto saved = (*backup)[QStringLiteral("adapters")].toObject();
        QStringList failures;
        const QString v4 = servers.ipv4.join(',');
        const QString v6 = servers.ipv6.join(',');
        for (const auto &adapter : adapters) {
            GUID guid{};
            if (!systemDnsGuid(adapter.guid, &guid)) continue;
            QString why;
            if (!saved.contains(adapter.guid)) {
                const auto original4 = systemDnsRead(guid, false, &why);
                const auto original6 = why.isEmpty() ? systemDnsRead(guid, true, &why) : QJsonObject();
                if (!why.isEmpty()) {
                    failures << QStringLiteral("%1: %2").arg(adapter.name, why);
                    continue;
                }
                // DNS the user set for this network in Windows (Settings, per network profile) is their choice, often
                // encrypted already; the adapter setting written below would override it. FDPI_SYSTEM_DNS_FORCE (tests)
                // changes the adapter anyway.
                const auto profile4 = original4[QStringLiteral("profile_servers")].toString();
                const auto profile6 = original6[QStringLiteral("profile_servers")].toString();
                if ((!profile4.isEmpty() || !profile6.isEmpty()) && !qEnvironmentVariableIsSet("FDPI_SYSTEM_DNS_FORCE")) {
                    if (untouched) *untouched << QStringLiteral("%1 (%2)").arg(adapter.name, QStringList{profile4, profile6}.join(' ').trimmed());
                    continue;
                }
                saved[adapter.guid] = QJsonObject{
                    {QStringLiteral("name"), adapter.name},
                    {QStringLiteral("v4"), original4},
                    {QStringLiteral("v6"), original6},
                };
            }
            auto entry = saved[adapter.guid].toObject();
            bool ok = systemDnsWrite(guid, false, v4, systemDnsDohFor(servers.ipv4, servers.dohTemplate), &why);
            if (ok && !servers.ipv6.isEmpty()) ok = systemDnsWrite(guid, true, v6, systemDnsDohFor(servers.ipv6, servers.dohTemplate), &why);
            entry[QStringLiteral("set_v4")] = v4;
            entry[QStringLiteral("set_v6")] = servers.ipv6.isEmpty() ? QString() : v6;
            saved[adapter.guid] = entry;
            if (!ok) failures << QStringLiteral("%1: %2").arg(adapter.name, why);
        }
        (*backup)[QStringLiteral("adapters")] = saved;
        FlushCache();
        if (!failures.isEmpty()) {
            *error = failures.join(QStringLiteral("; "));
            return false;
        }
        return true;
    }

    bool Restore(const QJsonObject &backup, QString *error) {
        if (!Supported()) return true;
        const auto saved = backup[QStringLiteral("adapters")].toObject();
        QStringList failures;
        for (auto it = saved.begin(); it != saved.end(); ++it) {
            GUID guid{};
            if (!systemDnsGuid(it.key(), &guid)) continue;
            const auto entry = it.value().toObject();
            const auto name = entry[QStringLiteral("name")].toString();
            for (const bool ipv6 : {false, true}) {
                const auto setKey = ipv6 ? QStringLiteral("set_v6") : QStringLiteral("set_v4");
                const auto ours = systemDnsSplit(entry[setKey].toString());
                if (ours.isEmpty()) continue;
                QString why;
                const auto current = systemDnsRead(guid, ipv6, &why);
                // Gone, or changed by someone else since: theirs now, not ours to put back.
                if (!why.isEmpty() || systemDnsSplit(current[QStringLiteral("servers")].toString()) != ours) continue;
                const auto original = entry[ipv6 ? QStringLiteral("v6") : QStringLiteral("v4")].toObject();
                if (!systemDnsWrite(guid, ipv6, original[QStringLiteral("servers")].toString(),
                                    systemDnsDohFrom(original[QStringLiteral("doh")].toArray()), &why)) {
                    failures << QStringLiteral("%1: %2").arg(name, why);
                }
            }
        }
        FlushCache();
        if (!failures.isEmpty()) {
            if (error) *error = failures.join(QStringLiteral("; "));
            return false;
        }
        return true;
    }

    QStringList EffectiveServers() {
        QStringList servers;
        for (const auto &adapter : systemDnsAdapters()) {
            for (const auto &server : adapter.servers) {
                if (!servers.contains(server)) servers << server;
            }
        }
        return servers;
    }

    void FlushCache() {
        using FlushFn = BOOL(WINAPI *)();
        static const FlushFn flush = [] {
            const HMODULE module = LoadLibraryW(L"dnsapi.dll");
            return module != nullptr ? reinterpret_cast<FlushFn>(GetProcAddress(module, "DnsFlushResolverCache")) : nullptr;
        }();
        if (flush != nullptr) flush();
    }
} // namespace Sys::SystemDns
