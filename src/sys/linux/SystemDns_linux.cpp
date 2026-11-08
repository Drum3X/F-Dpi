#include "include/sys/SystemDns.hpp"

// F-Dpi (Linux): systemd-resolved's per-link DNS, through the setuid core ("ThroneCore --fdpi-dns": one JSON request on stdin,
// one reply on stdout; core/internal/fdpidns). The backup and the "still ours?" decisions live here, as on Windows.
// Link settings set over D-Bus are runtime state: a reboot or the network manager's next update of the link drops them too.
#include <net/if.h>

#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QStandardPaths>

#include "include/global/Configs.hpp"
#include "include/sys/NetworkMonitor.hpp"

namespace Sys::SystemDns {
    namespace {
        constexpr int kSystemDnsCoreTimeoutMs = 15000;

        // One request to the core; the reply's "error" lands in *error.
        bool systemDnsCall(const QJsonObject &request, QJsonObject *reply, QString *error) {
            const QString core = Configs::FindCoreRealPath();
            if (core.isEmpty() || !QFile::exists(core)) {
                if (error) *error = QStringLiteral("the core was not found");
                return false;
            }
            QProcess p;
            p.setProgram(core);
            p.setArguments({QStringLiteral("--fdpi-dns")});
            p.start();
            if (!p.waitForStarted(5000)) {
                if (error) *error = QStringLiteral("the core could not be run: %1").arg(p.errorString());
                return false;
            }
            p.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
            p.closeWriteChannel();
            if (!p.waitForFinished(kSystemDnsCoreTimeoutMs)) {
                p.kill();
                p.waitForFinished(1000);
                if (error) *error = QStringLiteral("the core did not answer");
                return false;
            }
            const auto doc = QJsonDocument::fromJson(p.readAllStandardOutput());
            if (!doc.isObject()) {
                if (error) *error = QStringLiteral("the core gave no answer: %1").arg(QString::fromLocal8Bit(p.readAllStandardError()).trimmed());
                return false;
            }
            *reply = doc.object();
            const QString coreError = reply->value(QStringLiteral("error")).toString();
            if (!coreError.isEmpty()) {
                if (error) *error = coreError;
                return false;
            }
            return true;
        }

        QJsonArray systemDnsGet(int ifindex, QString *error) {
            QJsonObject reply;
            const QJsonObject request{{QStringLiteral("op"), QStringLiteral("get")},
                                      {QStringLiteral("links"), QJsonArray{QJsonObject{{QStringLiteral("ifindex"), ifindex}}}}};
            if (!systemDnsCall(request, &reply, error)) return {};
            return reply.value(QStringLiteral("links")).toArray();
        }

        QJsonObject systemDnsLink(int ifindex, QString *error) {
            const auto links = systemDnsGet(ifindex, error);
            return links.isEmpty() ? QJsonObject{} : links.first().toObject();
        }

        bool systemDnsSet(const QJsonObject &link, QString *error) {
            QJsonObject reply;
            return systemDnsCall({{QStringLiteral("op"), QStringLiteral("set")}, {QStringLiteral("links"), QJsonArray{link}}}, &reply, error);
        }

        QStringList systemDnsStrings(const QJsonValue &value) {
            QStringList out;
            for (const auto &item : value.toArray()) out << item.toString();
            return out;
        }

        // "1.1.1.1#cloudflare-dns.com" / "[2606::1]:853#x" -> the address alone.
        QString systemDnsAddress(QString server) {
            server = server.section('#', 0, 0);
            if (server.startsWith('[')) return server.mid(1, server.indexOf(']') - 1);
            if (server.count(':') == 1) return server.section(':', 0, 0);
            return server;
        }

        // The network manager's DNS servers the user typed in for this connection (nmcli ipv4.dns / ignore-auto-dns), the
        // Linux counterpart of a Windows network profile's DNS: F-Dpi leaves such a link alone.
        QString systemDnsUserServers(const QString &interfaceName) {
            const QString nmcli = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
            if (nmcli.isEmpty()) return {};
            const auto run = [&](const QStringList &args) {
                QProcess p;
                p.start(nmcli, args);
                if (!p.waitForFinished(3000) || p.exitCode() != 0) return QStringList{};
                return QString::fromUtf8(p.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
            };
            const auto connection = run({QStringLiteral("-g"), QStringLiteral("GENERAL.CONNECTION"), QStringLiteral("device"), QStringLiteral("show"), interfaceName});
            if (connection.isEmpty() || connection.first().trimmed().isEmpty()) return {};
            const auto values = run({QStringLiteral("-g"), QStringLiteral("ipv4.dns,ipv6.dns"), QStringLiteral("connection"), QStringLiteral("show"),
                                     QStringLiteral("id"), connection.first().trimmed()});
            QStringList servers;
            for (const auto &line : values) {
                for (const auto &server : line.split(',', Qt::SkipEmptyParts)) servers << server.trimmed();
            }
            return servers.join(',');
        }

        struct SystemDnsLinkName {
            QString name;
            int ifindex = 0;
        };

        SystemDnsLinkName systemDnsDefaultLink() {
            const auto network = NetworkMonitor::Latest();
            if (network.interfaceName.isEmpty()) return {};
            return {network.interfaceName, int(::if_nametoindex(network.interfaceName.toLocal8Bit().constData()))};
        }
    } // namespace

    bool Supported() {
        static QMutex mutex;
        static QElapsedTimer age;
        static bool cached = false;
        QMutexLocker locker(&mutex);
        if (age.isValid() && age.elapsed() < 30000) return cached;
        QJsonObject reply;
        cached = systemDnsCall({{QStringLiteral("op"), QStringLiteral("supported")}}, &reply, nullptr) &&
                 reply.value(QStringLiteral("supported")).toBool();
        age.start();
        return cached;
    }

    bool Apply(const Servers &servers, QJsonObject *backup, QStringList *untouched, QString *error) {
        if (servers.dotName.isEmpty()) {
            if (error) *error = QStringLiteral("this DNS provider has no DNS-over-TLS, which systemd-resolved needs");
            return false;
        }
        const auto link = systemDnsDefaultLink();
        if (link.ifindex <= 0) {
            if (error) *error = QStringLiteral("no connected network adapter");
            return false;
        }
        if (const auto user = systemDnsUserServers(link.name); !user.isEmpty()) {
            if (untouched) *untouched << QStringLiteral("%1 (%2)").arg(link.name, user);
            return true;
        }
        auto adapters = backup->value(QStringLiteral("adapters")).toObject();
        if (!adapters.contains(link.name)) {
            const auto current = systemDnsLink(link.ifindex, error);
            if (current.isEmpty()) return false;
            adapters[link.name] = QJsonObject{{QStringLiteral("ifindex"), link.ifindex}, {QStringLiteral("original"), current}};
        }

        QJsonArray set;
        for (const auto &address : servers.ipv4 + servers.ipv6) set << address + QLatin1Char('#') + servers.dotName;
        const QJsonObject wanted{{QStringLiteral("ifindex"), link.ifindex},
                                 {QStringLiteral("servers"), set},
                                 {QStringLiteral("dns_over_tls"), QStringLiteral("yes")},
                                 // This link answers every name, not only its own domains.
                                 {QStringLiteral("domains"), QJsonArray{QStringLiteral("~.")}},
                                 {QStringLiteral("default_route"), true}};
        auto entry = adapters[link.name].toObject();
        entry[QStringLiteral("ifindex")] = link.ifindex;
        entry[QStringLiteral("set")] = set;
        adapters[link.name] = entry;
        (*backup)[QStringLiteral("adapters")] = adapters;
        if (!systemDnsSet(wanted, error)) return false;
        FlushCache();
        return true;
    }

    bool Restore(const QJsonObject &backup, QString *error) {
        const auto adapters = backup.value(QStringLiteral("adapters")).toObject();
        QStringList failures;
        for (auto it = adapters.begin(); it != adapters.end(); ++it) {
            const auto entry = it.value().toObject();
            // Interfaces may be renumbered after a reconnect; the name is what identifies the adapter.
            const int ifindex = int(::if_nametoindex(it.key().toLocal8Bit().constData()));
            if (ifindex <= 0) continue;
            QString why;
            const auto current = systemDnsLink(ifindex, &why);
            if (current.isEmpty()) {
                failures << QStringLiteral("%1: %2").arg(it.key(), why);
                continue;
            }
            // Changed since (the user, or the network manager re-applying the link): leave it.
            if (systemDnsStrings(current.value(QStringLiteral("servers"))) != systemDnsStrings(entry.value(QStringLiteral("set")))) continue;
            auto original = entry.value(QStringLiteral("original")).toObject();
            original[QStringLiteral("ifindex")] = ifindex;
            if (!systemDnsSet(original, &why)) failures << QStringLiteral("%1: %2").arg(it.key(), why);
        }
        FlushCache();
        if (!failures.isEmpty()) {
            if (error) *error = failures.join(QStringLiteral("; "));
            return false;
        }
        return true;
    }

    QStringList EffectiveServers() {
        QStringList v4, v6;
        const auto link = systemDnsDefaultLink();
        if (link.ifindex > 0) {
            for (const auto &server : systemDnsStrings(systemDnsLink(link.ifindex, nullptr).value(QStringLiteral("servers")))) {
                const QString address = systemDnsAddress(server);
                (QHostAddress(address).protocol() == QAbstractSocket::IPv6Protocol ? v6 : v4) << address;
            }
        }
        if (v4.isEmpty() && v6.isEmpty()) {
            // Global servers (resolved.conf) or no resolved: what it forwards to.
            QFile file(QStringLiteral("/run/systemd/resolve/resolv.conf"));
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                for (const auto &line : QString::fromUtf8(file.readAll()).split('\n')) {
                    const auto fields = line.simplified().split(' ');
                    if (fields.size() >= 2 && fields[0] == QStringLiteral("nameserver")) {
                        (QHostAddress(fields[1]).protocol() == QAbstractSocket::IPv6Protocol ? v6 : v4) << fields[1];
                    }
                }
            }
        }
        return v4 + v6;
    }

    void FlushCache() {
        QJsonObject reply;
        systemDnsCall({{QStringLiteral("op"), QStringLiteral("flush")}}, &reply, nullptr);
    }
} // namespace Sys::SystemDns
