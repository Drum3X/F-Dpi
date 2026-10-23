#pragma once

#include <QList>
#include <QString>

namespace Dpi {
    // A built-in winws strategy. %ZAPRET% in the strategy is the winws directory (Dpi::ZapretDir()).
    struct Preset {
        QString id;
        // Untranslated; shown through PresetName().
        const char *name;
        QString wfTcp;
        QString wfUdp;
        QString strategy;
    };

    inline constexpr auto kDefaultPreset = "general";

    const QList<Preset> &Presets();

    const Preset *FindPreset(const QString &id);

    QString PresetName(const Preset &preset);
} // namespace Dpi
