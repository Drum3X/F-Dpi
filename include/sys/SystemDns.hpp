#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

// F-Dpi: the operating system's own resolver settings, for Otomatik DNS while DPI Bypass runs (sing-box, which would
// otherwise resolve, is idle then). Windows: per-adapter DNS servers with DNS-over-HTTPS required (Windows 11). Stubs
// elsewhere. Needs administrator rights. Any thread; calls are short and blocking.
namespace Sys::SystemDns {
    struct Servers {
        QStringList ipv4;
        QStringList ipv6;
        // DoH URL template Windows validates the servers' certificate against.
        QString dohTemplate;
    };

    bool Supported();

    // Points every connected physical adapter (Wi-Fi, Ethernet) at servers, DoH only with no plain fallback, and flushes
    // the cache. *backup collects what each adapter had before; an adapter already in it keeps its original entry, so
    // calling Apply again (another provider, a newly connected adapter) never loses the real original. An adapter whose
    // network has DNS servers set by the user in Windows is left alone and listed in *untouched ("name (servers)").
    bool Apply(const Servers &servers, QJsonObject *backup, QStringList *untouched, QString *error);

    // Puts back the adapters in backup. An adapter whose servers were changed since (by the user) or that is gone is left
    // alone. False with *error when an adapter could not be restored.
    bool Restore(const QJsonObject &backup, QString *error);

    // The servers connected physical adapters resolve with right now (DHCP or static), IPv4 first.
    QStringList EffectiveServers();

    void FlushCache();
} // namespace Sys::SystemDns
