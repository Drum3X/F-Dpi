#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "include/dpi/DpiLaunch.h"

namespace Dpi::Blockcheck {
    // What a strategy is tried against: plain HTTP on port 80, or TLS (1.2 and 1.3) on port 443.
    enum class Target { Http, Tls };

    // Quick scan: a short curated list, simplest first; the first one that unblocks every probe wins.
    QList<QString> QuickStrategies(Target target);

    // Deep scan: every combination of desync mode, split position, fooling and repeats; the best one wins.
    QList<QString> DeepStrategies(Target target);

    // The engine options that try one strategy alone: capture and filter on the target's port, then the strategy.
    // Windows captures with --wf-tcp; on Linux the core's nftables rule takes the port (Launch::tcpPorts).
    QStringList TrialArguments(Target target, const QString &strategy);
    Launch TrialLaunch(Target target, const QString &strategy);

    // A start error that every strategy would hit too (no privileges, no WinDivert/nftables/nfqws): the scan stops.
    bool IsEngineSetupError(const QString &error);

    // A dpi profile strategy from the winning HTTP and TLS strategies (either may be empty when not blocked), plus the
    // standard QUIC block (QUIC cannot be probed here; browsers fall back to TCP when it stays blocked).
    QString ComposeStrategy(const QString &http, const QString &tls);

    // The --wf-tcp ports ComposeStrategy needs.
    QString ComposePorts(const QString &http, const QString &tls);
} // namespace Dpi::Blockcheck
