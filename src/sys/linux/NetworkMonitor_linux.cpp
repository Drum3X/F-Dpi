#include "include/sys/NetworkMonitor.hpp"

#include <QByteArray>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/neighbour.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

// F-Dpi: Linux part of Sys::NetworkMonitor. Unprivileged: rtnetlink for routes and neighbours, NetworkManager and
// systemd-resolved over the system D-Bus when they run (neither does under systemd-networkd alone or WSL2).
namespace Sys {
    namespace {
        constexpr int kNetworkMonitorLinuxDbusTimeoutMs = 1000;
        const QString kNetworkMonitorLinuxNm = QStringLiteral("org.freedesktop.NetworkManager");
        const QString kNetworkMonitorLinuxResolved = QStringLiteral("org.freedesktop.resolve1");

        // ---- rtnetlink ----

        class NetworkMonitorLinuxSocket {
        public:
            explicit NetworkMonitorLinuxSocket(unsigned groups = 0) {
                m_fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
                if (m_fd < 0) return;
                sockaddr_nl local{};
                local.nl_family = AF_NETLINK;
                local.nl_groups = groups;
                if (bind(m_fd, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0) {
                    close(m_fd);
                    m_fd = -1;
                    return;
                }
                timeval timeout{1, 0};
                setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            }
            ~NetworkMonitorLinuxSocket() {
                if (m_fd >= 0) close(m_fd);
            }
            NetworkMonitorLinuxSocket(const NetworkMonitorLinuxSocket &) = delete;
            NetworkMonitorLinuxSocket &operator=(const NetworkMonitorLinuxSocket &) = delete;

            [[nodiscard]] int fd() const { return m_fd; }

            // A dump request (RTM_GETROUTE/RTM_GETNEIGH) for one family; onMessage sees every reply message.
            template<typename Body, typename Fn>
            bool dump(unsigned short type, const Body &body, Fn onMessage) {
                if (m_fd < 0) return false;
                struct {
                    nlmsghdr header;
                    Body body;
                } request{};
                request.header.nlmsg_len = NLMSG_LENGTH(sizeof(Body));
                request.header.nlmsg_type = type;
                request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
                request.header.nlmsg_seq = ++m_seq;
                request.body = body;
                sockaddr_nl kernel{};
                kernel.nl_family = AF_NETLINK;
                if (sendto(m_fd, &request, request.header.nlmsg_len, 0, reinterpret_cast<sockaddr *>(&kernel), sizeof(kernel)) < 0) {
                    return false;
                }
                std::vector<char> buffer(64 * 1024);
                for (;;) {
                    const ssize_t size = recv(m_fd, buffer.data(), buffer.size(), 0);
                    if (size < 0) {
                        if (errno == EINTR) continue;
                        return false;
                    }
                    if (size == 0) return false;
                    auto remaining = static_cast<unsigned>(size);
                    for (auto *message = reinterpret_cast<nlmsghdr *>(buffer.data()); NLMSG_OK(message, remaining);
                         message = NLMSG_NEXT(message, remaining)) {
                        if (message->nlmsg_seq != m_seq) continue;
                        if (message->nlmsg_type == NLMSG_DONE) return true;
                        if (message->nlmsg_type == NLMSG_ERROR) return false;
                        onMessage(message);
                    }
                }
            }

        private:
            int m_fd = -1;
            unsigned m_seq = 0;
        };

        struct NetworkMonitorLinuxRoute {
            int ifindex = 0;
            unsigned metric = 0;
            QByteArray gateway; // raw address bytes
        };

        QString networkMonitorLinuxAddress(int family, const void *bytes) {
            char text[INET6_ADDRSTRLEN] = {};
            if (inet_ntop(family, bytes, text, sizeof(text)) == nullptr) return {};
            return QString::fromLatin1(text);
        }

        // Default routes of the main table that have a gateway and a carrier; policy-routing tables (WireGuard, Tailscale)
        // are left out on purpose: the main table's default route is the physical uplink.
        std::vector<NetworkMonitorLinuxRoute> networkMonitorLinuxDefaultRoutes(NetworkMonitorLinuxSocket &netlink, unsigned char family) {
            std::vector<NetworkMonitorLinuxRoute> routes;
            const int addressLength = family == AF_INET ? 4 : 16;
            rtmsg query{};
            query.rtm_family = family;
            netlink.dump(RTM_GETROUTE, query, [&](const nlmsghdr *message) {
                if (message->nlmsg_type != RTM_NEWROUTE) return;
                const auto *route = static_cast<const rtmsg *>(NLMSG_DATA(message));
                if (route->rtm_family != family || route->rtm_dst_len != 0 || route->rtm_type != RTN_UNICAST) return;
                if ((route->rtm_flags & RTM_F_CLONED) != 0) return;
                unsigned table = route->rtm_table;
                int oif = 0;
                unsigned metric = 0;
                QByteArray gateway;
                const rtattr *multipath = nullptr;
                int length = static_cast<int>(RTM_PAYLOAD(message));
                for (auto *attribute = RTM_RTA(route); RTA_OK(attribute, length); attribute = RTA_NEXT(attribute, length)) {
                    switch (attribute->rta_type) {
                        case RTA_TABLE:
                            if (RTA_PAYLOAD(attribute) >= sizeof(unsigned)) std::memcpy(&table, RTA_DATA(attribute), sizeof(unsigned));
                            break;
                        case RTA_OIF:
                            if (RTA_PAYLOAD(attribute) >= sizeof(int)) std::memcpy(&oif, RTA_DATA(attribute), sizeof(int));
                            break;
                        case RTA_PRIORITY:
                            if (RTA_PAYLOAD(attribute) >= sizeof(unsigned)) std::memcpy(&metric, RTA_DATA(attribute), sizeof(unsigned));
                            break;
                        case RTA_GATEWAY:
                            if (static_cast<int>(RTA_PAYLOAD(attribute)) == addressLength) {
                                gateway = QByteArray(static_cast<const char *>(RTA_DATA(attribute)), addressLength);
                            }
                            break;
                        case RTA_MULTIPATH:
                            multipath = attribute;
                            break;
                        default:
                            break;
                    }
                }
                if (table != RT_TABLE_MAIN) return;
                constexpr unsigned kDown = RTNH_F_DEAD | RTNH_F_LINKDOWN;
                if (multipath == nullptr) {
                    if (oif > 0 && !gateway.isEmpty() && (route->rtm_flags & kDown) == 0) routes.push_back({oif, metric, gateway});
                    return;
                }
                // ECMP: every next hop is a candidate with the route's metric.
                int remaining = static_cast<int>(RTA_PAYLOAD(multipath));
                for (auto *hop = static_cast<const rtnexthop *>(RTA_DATA(multipath)); remaining >= static_cast<int>(sizeof(rtnexthop)) &&
                                                                                       hop->rtnh_len >= sizeof(rtnexthop) &&
                                                                                       hop->rtnh_len <= remaining;
                     remaining -= RTNH_ALIGN(hop->rtnh_len), hop = RTNH_NEXT(hop)) {
                    QByteArray hopGateway;
                    int hopLength = hop->rtnh_len - static_cast<int>(sizeof(rtnexthop));
                    for (auto *attribute = reinterpret_cast<const rtattr *>(RTNH_DATA(hop)); RTA_OK(attribute, hopLength);
                         attribute = RTA_NEXT(attribute, hopLength)) {
                        if (attribute->rta_type == RTA_GATEWAY && static_cast<int>(RTA_PAYLOAD(attribute)) == addressLength) {
                            hopGateway = QByteArray(static_cast<const char *>(RTA_DATA(attribute)), addressLength);
                        }
                    }
                    if (hop->rtnh_ifindex > 0 && !hopGateway.isEmpty() && (hop->rtnh_flags & kDown) == 0) {
                        routes.push_back({hop->rtnh_ifindex, metric, hopGateway});
                    }
                }
            });
            return routes;
        }

        QString networkMonitorLinuxNeighbourMac(NetworkMonitorLinuxSocket &netlink, int ifindex, const QByteArray &gateway) {
            QString mac;
            ndmsg query{};
            query.ndm_family = AF_INET;
            netlink.dump(RTM_GETNEIGH, query, [&](const nlmsghdr *message) {
                if (message->nlmsg_type != RTM_NEWNEIGH || !mac.isEmpty()) return;
                const auto *neighbour = static_cast<const ndmsg *>(NLMSG_DATA(message));
                if (neighbour->ndm_family != AF_INET || neighbour->ndm_ifindex != ifindex) return;
                if ((neighbour->ndm_state & (NUD_INCOMPLETE | NUD_FAILED)) != 0 || neighbour->ndm_state == NUD_NONE) return;
                QByteArray destination;
                QByteArray link;
                int length = static_cast<int>(message->nlmsg_len - NLMSG_LENGTH(sizeof(ndmsg)));
                for (auto *attribute = reinterpret_cast<const rtattr *>(reinterpret_cast<const char *>(neighbour) + NLMSG_ALIGN(sizeof(ndmsg)));
                     RTA_OK(attribute, length); attribute = RTA_NEXT(attribute, length)) {
                    const QByteArray value(static_cast<const char *>(RTA_DATA(attribute)), static_cast<int>(RTA_PAYLOAD(attribute)));
                    if (attribute->rta_type == NDA_DST) destination = value;
                    else if (attribute->rta_type == NDA_LLADDR) link = value;
                }
                if (destination != gateway || link.isEmpty() || link.count('\0') == link.size()) return;
                QStringList bytes;
                for (const char byte: link) bytes << QStringLiteral("%1").arg(static_cast<unsigned char>(byte), 2, 16, QLatin1Char('0'));
                mac = bytes.join('-'); // the Windows format: "gw:" keys stay comparable across both
            });
            return mac;
        }

        // The gateway's MAC address; when the kernel has not resolved it yet, one UDP datagram to the gateway makes it ARP.
        QString networkMonitorLinuxGatewayMac(NetworkMonitorLinuxSocket &netlink, int ifindex, const QByteArray &gateway) {
            auto mac = networkMonitorLinuxNeighbourMac(netlink, ifindex, gateway);
            if (!mac.isEmpty()) return mac;
            const int probe = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            if (probe < 0) return {};
            sockaddr_in target{};
            target.sin_family = AF_INET;
            target.sin_port = htons(9); // discard
            std::memcpy(&target.sin_addr, gateway.constData(), 4);
            sendto(probe, "", 0, MSG_DONTWAIT, reinterpret_cast<sockaddr *>(&target), sizeof(target));
            close(probe);
            for (int attempt = 0; attempt < 5 && mac.isEmpty(); ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                mac = networkMonitorLinuxNeighbourMac(netlink, ifindex, gateway);
            }
            return mac;
        }

        // ---- interfaces ----

        QString networkMonitorLinuxSysfs(const QString &interface, const char *entry) {
            return QStringLiteral("/sys/class/net/%1/%2").arg(interface, QLatin1String(entry));
        }

        // Real hardware only (Wi-Fi, Ethernet, USB tethering): virtual links have no "device", and the names cover the virtual
        // drivers that do have one (some VM NICs bridged into containers).
        bool networkMonitorLinuxPhysical(const QString &interface) {
            static const char *const kVirtual[] = {"lo", "tun", "tap", "wg", "veth", "br", "docker", "virbr", "vnet", "vmnet", "zt", "tailscale", "throne"};
            for (const char *prefix: kVirtual) {
                if (interface.startsWith(QLatin1String(prefix))) return false;
            }
            if (QFileInfo::exists(networkMonitorLinuxSysfs(interface, "bridge"))) return false;
            return QFileInfo::exists(networkMonitorLinuxSysfs(interface, "device"));
        }

        bool networkMonitorLinuxWireless(const QString &interface) {
            return QFileInfo::exists(networkMonitorLinuxSysfs(interface, "wireless")) ||
                   QFileInfo::exists(networkMonitorLinuxSysfs(interface, "phy80211"));
        }

        // This computer's IPv4 address on the interface; an APIPA (169.254.x.x) address only when it has nothing else.
        QString networkMonitorLinuxLocalIpv4(const QString &interface) {
            ifaddrs *list = nullptr;
            if (getifaddrs(&list) != 0) return {};
            QString address;
            QString fallback;
            const QByteArray name = interface.toLocal8Bit();
            for (auto *entry = list; entry != nullptr && address.isEmpty(); entry = entry->ifa_next) {
                if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET || name != entry->ifa_name) continue;
                const auto text = networkMonitorLinuxAddress(AF_INET, &reinterpret_cast<const sockaddr_in *>(entry->ifa_addr)->sin_addr);
                if (!text.startsWith(QStringLiteral("169.254."))) address = text;
                else if (fallback.isEmpty()) fallback = text;
            }
            freeifaddrs(list);
            return address.isEmpty() ? fallback : address;
        }

        // ---- D-Bus: NetworkManager, systemd-resolved ----

        // Only services that already run: a call to a stopped one would D-Bus-activate it (resolved) or time out.
        bool networkMonitorLinuxHasService(QDBusConnection &bus, const QString &service) {
            auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                                          QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameHasOwner"));
            message << service;
            const auto reply = bus.call(message, QDBus::Block, kNetworkMonitorLinuxDbusTimeoutMs);
            return reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty() && reply.arguments().first().toBool();
        }

        QVariant networkMonitorLinuxProperty(QDBusConnection &bus, const QString &service, const QString &path, const QString &interface,
                                             const QString &name) {
            if (path.isEmpty() || path == QLatin1String("/")) return {};
            auto message = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
            message << interface << name;
            const auto reply = bus.call(message, QDBus::Block, kNetworkMonitorLinuxDbusTimeoutMs);
            if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) return {};
            return reply.arguments().first().value<QDBusVariant>().variant();
        }

        QString networkMonitorLinuxPath(const QVariant &value) { return value.value<QDBusObjectPath>().path(); }

        struct NetworkMonitorLinuxNm {
            QString uuid;  // the active connection profile
            QString id;    // its name ("Home Wi-Fi", "Wired connection 1")
            QString ssid;
            QString dns;   // first IPv4 name server, else IPv6
        };

        QString networkMonitorLinuxNmIpv4Dns(QDBusConnection &bus, const QString &config) {
            const QString iface = QStringLiteral("org.freedesktop.NetworkManager.IP4Config");
            // NM >= 1.14: aa{sv} with "address"; the older "au" (network byte order) is deprecated but still there.
            const auto data = networkMonitorLinuxProperty(bus, kNetworkMonitorLinuxNm, config, iface, QStringLiteral("NameserverData"));
            if (data.canConvert<QDBusArgument>()) {
                const auto argument = data.value<QDBusArgument>();
                QString first;
                argument.beginArray();
                while (!argument.atEnd()) {
                    QVariantMap entry;
                    argument >> entry;
                    if (first.isEmpty()) first = entry.value(QStringLiteral("address")).toString();
                }
                argument.endArray();
                if (!first.isEmpty()) return first;
            }
            const auto legacy = qdbus_cast<QList<uint>>(networkMonitorLinuxProperty(bus, kNetworkMonitorLinuxNm, config, iface, QStringLiteral("Nameservers")));
            if (!legacy.isEmpty()) {
                const uint address = legacy.first(); // already network byte order
                return networkMonitorLinuxAddress(AF_INET, &address);
            }
            return {};
        }

        QString networkMonitorLinuxNmIpv6Dns(QDBusConnection &bus, const QString &config) {
            const auto value = networkMonitorLinuxProperty(bus, kNetworkMonitorLinuxNm, config, QStringLiteral("org.freedesktop.NetworkManager.IP6Config"),
                                                           QStringLiteral("Nameservers"));
            if (!value.canConvert<QDBusArgument>()) return {};
            const auto argument = value.value<QDBusArgument>();
            QString first;
            argument.beginArray();
            while (!argument.atEnd()) {
                QByteArray address;
                argument >> address;
                if (first.isEmpty() && address.size() == 16) first = networkMonitorLinuxAddress(AF_INET6, address.constData());
            }
            argument.endArray();
            return first;
        }

        bool networkMonitorLinuxNetworkManager(QDBusConnection &bus, const QString &interface, bool wifi, NetworkMonitorLinuxNm *out) {
            if (!networkMonitorLinuxHasService(bus, kNetworkMonitorLinuxNm)) return false;
            auto lookup = QDBusMessage::createMethodCall(kNetworkMonitorLinuxNm, QStringLiteral("/org/freedesktop/NetworkManager"), kNetworkMonitorLinuxNm,
                                                         QStringLiteral("GetDeviceByIpIface"));
            lookup << interface;
            const auto reply = bus.call(lookup, QDBus::Block, kNetworkMonitorLinuxDbusTimeoutMs);
            if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) return false; // unmanaged device
            const QString device = networkMonitorLinuxPath(reply.arguments().first());
            const QString deviceIface = QStringLiteral("org.freedesktop.NetworkManager.Device");

            const QString active = networkMonitorLinuxPath(networkMonitorLinuxProperty(bus, kNetworkMonitorLinuxNm, device, deviceIface, QStringLiteral("ActiveConnection")));
            const QString activeIface = QStringLiteral("org.freedesktop.NetworkManager.Connection.Active");
            out->uuid = networkMonitorLinuxProperty(bus, kNetworkMonitorLinuxNm, active, activeIface, QStringLiteral("Uuid")).toString();
            out->id = networkMonitorLinuxProperty(bus, kNetworkMonitorLinuxNm, active, activeIface, QStringLiteral("Id")).toString();
            if (wifi) {
                const QString accessPoint = networkMonitorLinuxPath(networkMonitorLinuxProperty(
                    bus, kNetworkMonitorLinuxNm, device, QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless"), QStringLiteral("ActiveAccessPoint")));
                const auto ssid = networkMonitorLinuxProperty(bus, kNetworkMonitorLinuxNm, accessPoint, QStringLiteral("org.freedesktop.NetworkManager.AccessPoint"),
                                                              QStringLiteral("Ssid"))
                                      .toByteArray();
                out->ssid = QString::fromUtf8(ssid);
            }
            out->dns = networkMonitorLinuxNmIpv4Dns(
                bus, networkMonitorLinuxPath(networkMonitorLinuxProperty(bus, kNetworkMonitorLinuxNm, device, deviceIface, QStringLiteral("Ip4Config"))));
            if (out->dns.isEmpty()) {
                out->dns = networkMonitorLinuxNmIpv6Dns(
                    bus, networkMonitorLinuxPath(networkMonitorLinuxProperty(bus, kNetworkMonitorLinuxNm, device, deviceIface, QStringLiteral("Ip6Config"))));
            }
            return true;
        }

        // systemd-resolved's per-link servers (networkd, or NM pushing its DNS there): IPv4 first.
        QString networkMonitorLinuxResolvedDns(QDBusConnection &bus, int ifindex) {
            if (!networkMonitorLinuxHasService(bus, kNetworkMonitorLinuxResolved)) return {};
            auto lookup = QDBusMessage::createMethodCall(kNetworkMonitorLinuxResolved, QStringLiteral("/org/freedesktop/resolve1"),
                                                         QStringLiteral("org.freedesktop.resolve1.Manager"), QStringLiteral("GetLink"));
            lookup << ifindex;
            const auto reply = bus.call(lookup, QDBus::Block, kNetworkMonitorLinuxDbusTimeoutMs);
            if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) return {};
            const auto value = networkMonitorLinuxProperty(bus, kNetworkMonitorLinuxResolved, networkMonitorLinuxPath(reply.arguments().first()),
                                                           QStringLiteral("org.freedesktop.resolve1.Link"), QStringLiteral("DNS"));
            if (!value.canConvert<QDBusArgument>()) return {};
            const auto argument = value.value<QDBusArgument>(); // a(iay)
            QString ipv4;
            QString ipv6;
            argument.beginArray();
            while (!argument.atEnd()) {
                int family = 0;
                QByteArray address;
                argument.beginStructure();
                argument >> family >> address;
                argument.endStructure();
                if (family == AF_INET && address.size() == 4 && ipv4.isEmpty()) ipv4 = networkMonitorLinuxAddress(AF_INET, address.constData());
                if (family == AF_INET6 && address.size() == 16 && ipv6.isEmpty()) ipv6 = networkMonitorLinuxAddress(AF_INET6, address.constData());
            }
            argument.endArray();
            return ipv4.isEmpty() ? ipv6 : ipv4;
        }

        // Neither NM nor resolved (plain DHCP client, WSL2): /etc/resolv.conf, unless it points at a local stub.
        QString networkMonitorLinuxResolvConfDns() {
            QFile file(QStringLiteral("/etc/resolv.conf"));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
            while (!file.atEnd()) {
                const auto parts = QString::fromLatin1(file.readLine()).simplified().split(' ');
                if (parts.size() < 2 || parts[0] != QLatin1String("nameserver")) continue;
                const auto &server = parts[1];
                if (server.startsWith(QStringLiteral("127.")) || server == QLatin1String("::1")) return {};
                return server;
            }
            return {};
        }

        // ---- change watch ----

        struct NetworkMonitorLinuxWatch {
            std::function<void()> onChange;
            std::unique_ptr<NetworkMonitorLinuxSocket> netlink;
            int stopFd = -1;
            std::thread reader;
        };

        // Wireless-extension events (scan results) arrive as RTM_NEWLINK with IFLA_WIRELESS; they are not network changes and
        // would keep restarting the debounce.
        bool networkMonitorLinuxWirelessEvent(const nlmsghdr *message) {
            if (message->nlmsg_type != RTM_NEWLINK) return false;
            const auto *link = static_cast<const ifinfomsg *>(NLMSG_DATA(message));
            int length = static_cast<int>(message->nlmsg_len - NLMSG_LENGTH(sizeof(ifinfomsg)));
            for (auto *attribute = IFLA_RTA(link); RTA_OK(attribute, length); attribute = RTA_NEXT(attribute, length)) {
                if (attribute->rta_type == IFLA_WIRELESS) return true;
            }
            return false;
        }

        void networkMonitorLinuxRead(NetworkMonitorLinuxWatch *watch) {
            std::vector<char> buffer(64 * 1024);
            pollfd fds[2] = {{watch->netlink->fd(), POLLIN, 0}, {watch->stopFd, POLLIN, 0}};
            for (;;) {
                fds[0].revents = fds[1].revents = 0;
                if (poll(fds, 2, -1) < 0) {
                    if (errno == EINTR) continue;
                    return;
                }
                if (fds[1].revents != 0) return;
                if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && (fds[0].revents & POLLIN) == 0) return;
                bool changed = false;
                for (;;) {
                    const ssize_t size = recv(watch->netlink->fd(), buffer.data(), buffer.size(), MSG_DONTWAIT);
                    if (size < 0) {
                        if (errno == EINTR) continue;
                        if (errno == ENOBUFS) changed = true; // dropped events: something did change
                        break;
                    }
                    if (size == 0) break;
                    auto remaining = static_cast<unsigned>(size);
                    for (auto *message = reinterpret_cast<const nlmsghdr *>(buffer.data()); NLMSG_OK(message, remaining);
                         message = NLMSG_NEXT(message, remaining)) {
                        if (message->nlmsg_type == NLMSG_DONE || message->nlmsg_type == NLMSG_ERROR || message->nlmsg_type == NLMSG_NOOP) continue;
                        if (!networkMonitorLinuxWirelessEvent(message)) changed = true;
                    }
                }
                if (changed) watch->onChange();
            }
        }
    } // namespace

    NetworkIdentity DetectNetwork() {
        NetworkMonitorLinuxSocket netlink;
        if (netlink.fd() < 0) return {};

        // The interface Linux routes through: the lowest IPv4 metric among the physical ones with a gateway.
        const NetworkMonitorLinuxRoute *best = nullptr;
        QString bestName;
        const auto routes = networkMonitorLinuxDefaultRoutes(netlink, AF_INET);
        for (const auto &route: routes) {
            char name[IF_NAMESIZE] = {};
            if (if_indextoname(static_cast<unsigned>(route.ifindex), name) == nullptr) continue;
            const auto interface = QString::fromLocal8Bit(name);
            if (!networkMonitorLinuxPhysical(interface)) continue;
            if (best == nullptr || route.metric < best->metric) {
                best = &route;
                bestName = interface;
            }
        }
        if (best == nullptr) return {};

        const bool wifi = networkMonitorLinuxWireless(bestName);
        const auto gatewayAddress = networkMonitorLinuxAddress(AF_INET, best->gateway.constData());

        NetworkMonitorLinuxNm nm;
        bool hasNm = false;
        QString resolvedDns;
        if (auto bus = QDBusConnection::systemBus(); bus.isConnected()) {
            hasNm = networkMonitorLinuxNetworkManager(bus, bestName, wifi, &nm);
            if (nm.dns.isEmpty()) resolvedDns = networkMonitorLinuxResolvedDns(bus, best->ifindex);
        }

        NetworkIdentity network;
        network.kind = wifi ? NetworkIdentity::Kind::WiFi : NetworkIdentity::Kind::Ethernet;
        // NM's connection profile names a Wi-Fi network (one profile per SSID), but a wired profile ("Wired connection 1")
        // is reused on every cable: as a key it would make all wired networks the same one, so wired relies on the gateway.
        if (hasNm && wifi && !nm.uuid.isEmpty()) network.keys << QStringLiteral("nm:") + nm.uuid;
        if (const auto mac = networkMonitorLinuxGatewayMac(netlink, best->ifindex, best->gateway); !mac.isEmpty()) {
            network.keys << QStringLiteral("gw:") + mac;
        }
        if (!nm.ssid.isEmpty()) network.keys << QStringLiteral("wifi:") + nm.ssid;
        if (network.keys.isEmpty()) network.keys << QStringLiteral("gwip:") + gatewayAddress;
        network.key = network.keys.first();
        if (!nm.id.isEmpty()) network.name = nm.id;
        else if (!nm.ssid.isEmpty()) network.name = nm.ssid;
        else network.name = QStringLiteral("%1 (%2)").arg(bestName, gatewayAddress);
        network.gateway = gatewayAddress;
        // The IPv6 default gateway on the same interface; usually link-local (fe80::...), without the zone.
        const NetworkMonitorLinuxRoute *best6 = nullptr;
        const auto routes6 = networkMonitorLinuxDefaultRoutes(netlink, AF_INET6);
        for (const auto &route: routes6) {
            if (route.ifindex == best->ifindex && (best6 == nullptr || route.metric < best6->metric)) best6 = &route;
        }
        if (best6 != nullptr) network.gateway6 = networkMonitorLinuxAddress(AF_INET6, best6->gateway.constData());
        network.localIp = networkMonitorLinuxLocalIpv4(bestName);
        network.dhcpDns = !nm.dns.isEmpty() ? nm.dns : !resolvedDns.isEmpty() ? resolvedDns : networkMonitorLinuxResolvConfDns();
        network.interfaceName = bestName;
        return network;
    }

    void *WatchNetworkChanges(std::function<void()> onChange) {
        auto watch = std::make_unique<NetworkMonitorLinuxWatch>();
        watch->onChange = std::move(onChange);
        watch->netlink = std::make_unique<NetworkMonitorLinuxSocket>(RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE);
        if (watch->netlink->fd() < 0) return nullptr;
        watch->stopFd = eventfd(0, EFD_CLOEXEC);
        if (watch->stopFd < 0) return nullptr;
        watch->reader = std::thread(networkMonitorLinuxRead, watch.get());
        return watch.release();
    }

    void UnwatchNetworkChanges(void *handle) {
        auto *watch = static_cast<NetworkMonitorLinuxWatch *>(handle);
        if (watch == nullptr) return;
        const uint64_t one = 1;
        if (write(watch->stopFd, &one, sizeof(one)) < 0) {
            // eventfd writes only fail on overflow; the reader still sees it readable
        }
        if (watch->reader.joinable()) watch->reader.join();
        close(watch->stopFd);
        delete watch;
    }
} // namespace Sys
