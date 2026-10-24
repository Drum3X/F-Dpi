#include "include/sys/NetworkPlaceholders.h"

#include <QCoreApplication>
#include <QMutex>
#include <QRegularExpression>

#include "include/sys/NetworkMonitor.hpp"

namespace Sys {
    namespace {
        const QRegularExpression &networkPlaceholderPattern() {
            static const QRegularExpression pattern(QStringLiteral("%([A-Za-z0-9_]+)%"));
            return pattern;
        }

        QMutex networkPlaceholderDnsMutex;
        std::function<QStringList()> networkPlaceholderOriginalDns;

        QString networkPlaceholderMissing(const QString &name, bool connected) {
            const QString token = QLatin1Char('%') + name + QLatin1Char('%');
            if (!connected) return QCoreApplication::translate("NetworkPlaceholders", "%1: there is no network connection.").arg(token);
            if (name == QLatin1String("GATEWAY")) return QCoreApplication::translate("NetworkPlaceholders", "%1: this network has no IPv4 gateway.").arg(token);
            if (name == QLatin1String("GATEWAY6")) return QCoreApplication::translate("NetworkPlaceholders", "%1: this network has no IPv6 gateway.").arg(token);
            if (name == QLatin1String("LOCAL_IP")) return QCoreApplication::translate("NetworkPlaceholders", "%1: this computer has no IPv4 address on this network.").arg(token);
            if (name == QLatin1String("DHCP_DNS")) return QCoreApplication::translate("NetworkPlaceholders", "%1: this network gives no DNS server.").arg(token);
            return QCoreApplication::translate("NetworkPlaceholders", "%1 has no value on this network.").arg(token);
        }

        // Replaces each known placeholder with value(name); unknown ones stay.
        template <typename Value>
        QString networkPlaceholderReplace(const QString &text, Value value) {
            if (!text.contains(QLatin1Char('%'))) return text;
            QString out;
            qsizetype last = 0;
            auto it = networkPlaceholderPattern().globalMatch(text);
            while (it.hasNext()) {
                const auto match = it.next();
                const auto name = match.captured(1).toUpper();
                if (!NetworkPlaceholderNames().contains(name)) continue;
                out += QStringView(text).mid(last, match.capturedStart() - last);
                out += value(name);
                last = match.capturedEnd();
            }
            out += QStringView(text).mid(last);
            return out;
        }
    } // namespace

    const QStringList &NetworkPlaceholderNames() {
        static const QStringList names = {QStringLiteral("GATEWAY"), QStringLiteral("GATEWAY6"), QStringLiteral("LOCAL_IP"),
                                          QStringLiteral("DHCP_DNS"), QStringLiteral("INTERFACE")};
        return names;
    }

    QStringList NetworkPlaceholdersIn(const QString &text) {
        QStringList found;
        if (!text.contains(QLatin1Char('%'))) return found;
        auto it = networkPlaceholderPattern().globalMatch(text);
        while (it.hasNext()) {
            const auto name = it.next().captured(1).toUpper();
            if (NetworkPlaceholderNames().contains(name) && !found.contains(name)) found << name;
        }
        return found;
    }

    QMap<QString, QString> NetworkPlaceholderValues() {
        const auto network = NetworkMonitor::Latest();
        QString dns = network.dhcpDns;
        {
            QMutexLocker lock(&networkPlaceholderDnsMutex);
            if (networkPlaceholderOriginalDns) {
                // F-Dpi's own DNS is on the adapter now; the one the network gave is in the backup.
                if (const auto original = networkPlaceholderOriginalDns(); !original.isEmpty()) dns = original.first();
            }
        }
        if (!network.connected()) return {};
        return {
            {QStringLiteral("GATEWAY"), network.gateway},
            {QStringLiteral("GATEWAY6"), network.gateway6},
            {QStringLiteral("LOCAL_IP"), network.localIp},
            {QStringLiteral("DHCP_DNS"), dns},
            {QStringLiteral("INTERFACE"), network.interfaceName},
        };
    }

    QString ExpandNetworkPlaceholders(const QString &text, QString *error) {
        const auto names = NetworkPlaceholdersIn(text);
        if (names.isEmpty()) return text;
        const auto values = NetworkPlaceholderValues();
        for (const auto &name : names) {
            if (values.value(name).isEmpty()) {
                if (error != nullptr) *error = networkPlaceholderMissing(name, !values.isEmpty());
                return {};
            }
        }
        return networkPlaceholderReplace(text, [&values](const QString &name) { return values.value(name); });
    }

    QString WithSampleNetworkValues(const QString &text) {
        return networkPlaceholderReplace(text, [](const QString &name) -> QString {
            if (name == QLatin1String("GATEWAY6")) return QStringLiteral("2001:db8::1");
            if (name == QLatin1String("LOCAL_IP")) return QStringLiteral("192.0.2.2");
            if (name == QLatin1String("DHCP_DNS")) return QStringLiteral("192.0.2.53");
            if (name == QLatin1String("INTERFACE")) return QStringLiteral("Wi-Fi");
            return QStringLiteral("192.0.2.1");
        });
    }

    void SetOriginalDnsLookup(std::function<QStringList()> lookup) {
        QMutexLocker lock(&networkPlaceholderDnsMutex);
        networkPlaceholderOriginalDns = std::move(lookup);
    }
} // namespace Sys
