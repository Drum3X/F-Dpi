#pragma once

#include <functional>

#include <QMap>
#include <QString>
#include <QStringList>

// F-Dpi: network placeholders, for addresses that change with the network (a phone hotspot hands out a new gateway on
// every connection). Every value comes from the adapter the default route leaves through, as Sys::NetworkMonitor detects
// it (Wi-Fi or Ethernet; never Wintun or TAP):
//   %GATEWAY%   the IPv4 default gateway        %GATEWAY6%  the IPv6 default gateway
//   %LOCAL_IP%  this computer's IPv4 address    %DHCP_DNS%  the adapter's first DNS server, as it was before F-Dpi
//   %INTERFACE% Windows' name for the adapter               changed it for DPI Bypass
// Names are case-insensitive; an unknown %NAME% stays as it is. Any thread.
namespace Sys {
    // The upper-case names, without the percent signs.
    const QStringList &NetworkPlaceholderNames();

    // The known placeholders text names, upper-case, each once, in order.
    QStringList NetworkPlaceholdersIn(const QString &text);

    inline bool HasNetworkPlaceholders(const QString &text) {
        return text.contains(QLatin1Char('%')) && !NetworkPlaceholdersIn(text).isEmpty();
    }

    // name -> value now; empty when the network has none.
    QMap<QString, QString> NetworkPlaceholderValues();

    // text with its placeholders replaced. A placeholder without a value fails: *error says which, and {} is returned.
    QString ExpandNetworkPlaceholders(const QString &text, QString *error);

    // text with sample values (192.0.2.1, "Wi-Fi"...), for validating a field that holds placeholders.
    QString WithSampleNetworkValues(const QString &text);

    // While F-Dpi has pointed Windows at its own DNS (DPI Bypass), the servers the adapters had before, IPv4 first;
    // empty otherwise. Set once by the engine (Otomatik DNS).
    void SetOriginalDnsLookup(std::function<QStringList()> lookup);
} // namespace Sys
