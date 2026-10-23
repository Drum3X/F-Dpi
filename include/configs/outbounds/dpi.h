#pragma once
#include "include/configs/common/Outbound.h"

namespace Configs
{
    // F-Dpi: a DPI bypass profile. It runs zapret's winws instead of the core, so it is never a proxy: Build() refuses, and the
    // engine starts it through Dpi::BuildLaunch() (include/dpi/DpiLaunch.h).
    class dpi : public outbound {
    public:
        // Split bypass: desync every matching connection, only hosts on the list, or hosts winws finds blocked (plus the list).
        static constexpr auto ModeAll = "all";
        static constexpr auto ModeHostlist = "hostlist";
        static constexpr auto ModeAutoHostlist = "autohostlist";

        // A Dpi::Preset id, or empty once the strategy was edited by hand.
        QString preset;
        // winws profile options, one or more blocks separated by --new; # starts a comment; %ZAPRET% is the winws directory.
        QString strategy;
        // WinDivert capture ports (--wf-tcp / --wf-udp), e.g. "80,443"; empty captures nothing of that protocol.
        QString wfTcp = "80,443";
        QString wfUdp = "443";
        QString mode = ModeAll;
        QStringList hostlist;
        QStringList exclude;

        // New profiles start from Dpi::kDefaultPreset.
        dpi();

        // Copies the preset strategy and ports; false for an unknown id.
        bool ApplyPreset(const QString &id);

        bool HasServerEndpoint() override { return false; }

        bool IsDpi() override { return true; }

        bool ParseFromJson(const QJsonObject &object) override;

        QJsonObject ExportToJson() override;

        QString DisplayType() override { return "DPI Bypass"; }

        QString DisplayAddress() override;

        // Nothing is encrypted by a DPI bypass.
        SecurityInfo GetSecurity() override { return {}; }

        void Validate(Validation::Issues &issues) override;

        BuildResult Build() override;

        // The strategy as winws arguments, comments dropped; error on unbalanced quotes.
        QStringList StrategyTokens(QString *error = nullptr) const;
    };

    namespace Checks {
        // A winws port list: comma-separated ports or ranges (80,443,1000-2000); empty is allowed.
        QString DpiPorts(const QString &text);
        // The strategy must give winws something to do and must not set what F-Dpi manages (files it writes, lists, debug output).
        QString DpiStrategy(const QString &text);
    }
}
