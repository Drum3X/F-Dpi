#pragma once

#include <QList>
#include <QStringList>
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

    // A ready-made site list the editor can add in one click.
    struct SiteList {
        QString id;
        // Untranslated; shown through SiteListName().
        const char *name;
        QStringList hosts;
        // Also read from <zapret>/files/<zapretFile> when set (zapret-win-bundle ships some lists).
        QString zapretFile;
    };

    // Commonly blocked services, for "listed sites only".
    const QList<SiteList> &BlockedSiteLists();

    // Services that must not be touched (game launchers and stores), for the exclude list.
    const QList<SiteList> &ExcludedSiteLists();

    QString SiteListName(const SiteList &list);

    QStringList SiteListHosts(const SiteList &list);
} // namespace Dpi
