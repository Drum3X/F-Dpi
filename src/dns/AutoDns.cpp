#include "include/dns/AutoDns.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QPair>

#include "include/database/DatabaseManager.h"
#include "include/database/SettingsRepo.h"
#include "include/database/entities/RouteProfile.h"
#include "include/sys/NetworkPlaceholders.h"
#include "include/sys/SystemDns.hpp"

namespace Dns {
    const QList<Provider> &Providers() {
        static const QList<Provider> providers = {
            {QStringLiteral("cloudflare"), QStringLiteral("Cloudflare"), QStringLiteral("https://1.1.1.1/dns-query"),
             QStringLiteral("https://cloudflare-dns.com/dns-query"), {QStringLiteral("1.1.1.1"), QStringLiteral("1.0.0.1")},
             {QStringLiteral("2606:4700:4700::1111"), QStringLiteral("2606:4700:4700::1001")}},
            {QStringLiteral("google"), QStringLiteral("Google"), QStringLiteral("https://8.8.8.8/dns-query"),
             QStringLiteral("https://dns.google/dns-query"), {QStringLiteral("8.8.8.8"), QStringLiteral("8.8.4.4")},
             {QStringLiteral("2001:4860:4860::8888"), QStringLiteral("2001:4860:4860::8844")}},
            {QStringLiteral("quad9"), QStringLiteral("Quad9"), QStringLiteral("https://9.9.9.9/dns-query"),
             QStringLiteral("https://dns.quad9.net/dns-query"), {QStringLiteral("9.9.9.9"), QStringLiteral("149.112.112.112")},
             {QStringLiteral("2620:fe::fe"), QStringLiteral("2620:fe::9")}},
            {QStringLiteral("adguard"), QStringLiteral("AdGuard (unfiltered)"), QStringLiteral("https://94.140.14.140/dns-query"),
             QStringLiteral("https://unfiltered.adguard-dns.com/dns-query"), {QStringLiteral("94.140.14.140"), QStringLiteral("94.140.14.141")},
             {QStringLiteral("2a10:50c0::1:ff"), QStringLiteral("2a10:50c0::2:ff")}},
            {QStringLiteral("opendns"), QStringLiteral("OpenDNS"), QStringLiteral("https://208.67.222.222/dns-query"),
             QStringLiteral("https://doh.opendns.com/dns-query"), {QStringLiteral("208.67.222.222"), QStringLiteral("208.67.220.220")},
             {QStringLiteral("2620:119:35::35"), QStringLiteral("2620:119:53::53")}},
            {QStringLiteral("mullvad"), QStringLiteral("Mullvad"), QStringLiteral("https://194.242.2.2/dns-query"),
             QStringLiteral("https://dns.mullvad.net/dns-query"), {QStringLiteral("194.242.2.2")}, {QStringLiteral("2a07:e340::2")}},
        };
        return providers;
    }

    const Provider *FindProvider(const QString &id) {
        for (const auto &provider : Providers()) {
            if (provider.id == id) return &provider;
        }
        return nullptr;
    }

    const Provider *ActiveProvider() {
        const auto &settings = Configs::dataManager->settingsRepo;
        if (settings->auto_dns_mode == kModeOff) return nullptr;
        if (settings->auto_dns_mode != kModeAuto) {
            if (const auto *fixed = FindProvider(settings->auto_dns_mode)) return fixed;
        }
        if (const auto *picked = FindProvider(settings->auto_dns_server)) return picked;
        return FindProvider(kDefaultProvider);
    }

    bool IsBogon(const QHostAddress &address) {
        if (address.isNull() || address.isLoopback() || address.isMulticast() || address.isLinkLocal() || address.isUniqueLocalUnicast() ||
            address.isBroadcast()) {
            return true;
        }
        bool mapped = false;
        const quint32 v4 = address.toIPv4Address(&mapped);
        if (mapped) {
            static const QList<QPair<quint32, int>> ranges = {
                {0x00000000, 8},  {0x0A000000, 8},  {0x64400000, 10}, {0x7F000000, 8},  {0xA9FE0000, 16}, {0xAC100000, 12},
                {0xC0000000, 24}, {0xC0000200, 24}, {0xC0A80000, 16}, {0xC6120000, 15}, {0xC6336400, 24}, {0xCB007100, 24},
                {0xE0000000, 4},  {0xF0000000, 4},
            };
            for (const auto &[network, bits] : ranges) {
                if ((v4 >> (32 - bits)) == (network >> (32 - bits))) return true;
            }
            return false;
        }
        static const QList<QPair<QHostAddress, int>> ranges6 = {
            {QHostAddress(QStringLiteral("::")), 96},
            {QHostAddress(QStringLiteral("100::")), 64},
            {QHostAddress(QStringLiteral("2001:db8::")), 32},
        };
        for (const auto &range : ranges6) {
            if (address.isInSubnet(range)) return true;
        }
        return false;
    }

    QStringList DefaultTestDomains() {
        return {QStringLiteral("discord.com"), QStringLiteral("www.youtube.com"), QStringLiteral("x.com"), QStringLiteral("telegram.org"),
                QStringLiteral("signal.org")};
    }

    bool ApplyToRouteDns(Configs::RouteDns *dns, bool routeOwnsDns) {
        if (routeOwnsDns || !Configs::dataManager->settingsRepo->auto_dns_proxy) return false;
        const auto *provider = ActiveProvider();
        if (provider == nullptr || dns->direct_dns == provider->url) return false;
        dns->direct_dns = provider->url;
        return true;
    }

    void ExpandRouteDnsPlaceholders(Configs::RouteDns *dns, QString *error) {
        const auto expand = [error](QString *server, const QString &what) {
            if (!Sys::HasNetworkPlaceholders(*server)) return;
            QString problem;
            const auto expanded = Sys::ExpandNetworkPlaceholders(*server, &problem);
            if (expanded.isEmpty()) *error += QStringLiteral("%1: %2").arg(what, problem);
            else *server = expanded;
        };
        expand(&dns->direct_dns, QCoreApplication::translate("AutoDns", "Direct DNS"));
        expand(&dns->remote_dns, QCoreApplication::translate("AutoDns", "Remote DNS"));
    }

    bool RestoreSystemDnsBackup(const QString &backupFile, QString *error) {
        QFile file(backupFile);
        if (!file.open(QIODevice::ReadOnly)) return true;
        const auto backup = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        const bool ok = Sys::SystemDns::Restore(backup, error);
        QFile::remove(backupFile);
        return ok;
    }
} // namespace Dns
