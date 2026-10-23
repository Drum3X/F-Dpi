#pragma once

#include <QHostAddress>
#include <QList>
#include <QString>
#include <QStringList>

namespace Configs {
    struct RouteDns;
}

// F-Dpi Otomatik DNS: picks a fast encrypted resolver for the user so a poisoned ISP resolver cannot redirect or hide sites.
// - Proxy profiles: the chosen DoH server becomes sing-box's direct DNS (the remote one already goes through the proxy).
// - DPI Bypass: winws does not touch DNS, so while it runs the Windows adapters use the server with DoH required
//   (Engine::AutoDnsService, Sys::SystemDns), restored afterwards.
// Settings: auto_dns_mode ("auto" = fastest by benchmark, "off", or a provider id), auto_dns_proxy, auto_dns_system,
// and the benchmark's last pick auto_dns_server / auto_dns_latency / auto_dns_network / auto_dns_checked.
namespace Dns {
    struct Provider {
        QString id;
        QString name;
        // Pinned by IP, so no resolver is needed to reach it; the certificate covers the IP (checked by the benchmark).
        QString url;
        // What Windows' DoH client is given for these addresses; it validates the certificate against this host.
        QString dohTemplate;
        QStringList ipv4;
        QStringList ipv6;
    };

    inline const QString kModeAuto = QStringLiteral("auto");
    inline const QString kModeOff = QStringLiteral("off");
    inline const QString kDefaultProvider = QStringLiteral("cloudflare");

    // Unfiltered public resolvers only: a filtering one could block what the user is trying to reach.
    const QList<Provider> &Providers();
    const Provider *FindProvider(const QString &id);

    // What Auto DNS uses now: the fixed provider, else the benchmark's pick, else the default. nullptr when it is off.
    const Provider *ActiveProvider();

    // Addresses no public site resolves to: what a poisoning resolver answers with (0.0.0.0, loopback, private ranges...).
    bool IsBogon(const QHostAddress &address);

    // Blocked in many countries; the poisoning test and the benchmark's filtering check resolve these.
    QStringList DefaultTestDomains();

    // generate.cpp hook: points the direct DNS at the active provider unless Auto DNS is off for proxies or the routing
    // profile brings its own DNS. True when it changed dns.
    bool ApplyToRouteDns(Configs::RouteDns *dns, bool routeOwnsDns);
} // namespace Dns
