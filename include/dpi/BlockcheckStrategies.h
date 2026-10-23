#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Dpi::Blockcheck {
    // What a strategy is tried against: plain HTTP on port 80, or TLS (1.2 and 1.3) on port 443.
    enum class Target { Http, Tls };

    // Quick scan: a short curated list, simplest first; the first one that unblocks every probe wins.
    QList<QString> QuickStrategies(Target target);

    // Deep scan: every combination of desync mode, split position, fooling and repeats; the best one wins.
    QList<QString> DeepStrategies(Target target);

    // The winws options that try one strategy alone: capture and filter on the target's port, then the strategy.
    QStringList TrialArguments(Target target, const QString &strategy);

    // A dpi profile strategy from the winning HTTP and TLS strategies (either may be empty when not blocked), plus the
    // standard QUIC block (QUIC cannot be probed here; browsers fall back to TCP when it stays blocked).
    QString ComposeStrategy(const QString &http, const QString &tls);

    // The --wf-tcp ports ComposeStrategy needs.
    QString ComposePorts(const QString &http, const QString &tls);
} // namespace Dpi::Blockcheck
