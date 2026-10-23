#include "include/dpi/DpiPresets.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include "include/dpi/DpiLaunch.h"

namespace Dpi {
    const QList<Preset> &Presets() {
        // Derived from zapret-win-bundle's preset1_example.cmd. Blockcheck (F5) finds a strategy for the user's own network.
        static const QList<Preset> presets = {
            {
                QStringLiteral("general"),
                QT_TRANSLATE_NOOP("Dpi", "General (fake + multidisorder)"),
                QStringLiteral("80,443"),
                QStringLiteral("443"),
                QStringLiteral("--filter-tcp=80 --dpi-desync=fake,fakedsplit --dpi-desync-autottl=2 --dpi-desync-fooling=md5sig\n"
                               "--new\n"
                               "--filter-tcp=443 --dpi-desync=fake,multidisorder --dpi-desync-split-pos=1,midsld "
                               "--dpi-desync-repeats=6 --dpi-desync-fooling=badseq,md5sig\n"
                               "--new\n"
                               "--filter-udp=443 --filter-l7=quic --dpi-desync=fake --dpi-desync-repeats=11 "
                               "--dpi-desync-fake-quic=\"%ZAPRET%\\files\\quic_initial_www_google_com.bin\"\n"),
            },
            {
                QStringLiteral("split"),
                QT_TRANSLATE_NOOP("Dpi", "Light (multisplit, no fake packets)"),
                QStringLiteral("80,443"),
                QString(),
                QStringLiteral("--filter-tcp=80,443 --dpi-desync=multisplit --dpi-desync-split-pos=1,midsld\n"),
            },
            {
                QStringLiteral("fake-ttl"),
                QT_TRANSLATE_NOOP("Dpi", "Fake with low TTL"),
                QStringLiteral("80,443"),
                QStringLiteral("443"),
                QStringLiteral("--filter-tcp=80,443 --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-ttl=4\n"
                               "--new\n"
                               "--filter-udp=443 --filter-l7=quic --dpi-desync=fake --dpi-desync-repeats=6\n"),
            },
        };
        return presets;
    }

    const Preset *FindPreset(const QString &id) {
        for (const auto &preset : Presets()) {
            if (preset.id == id) return &preset;
        }
        return nullptr;
    }

    QString PresetName(const Preset &preset) {
        return QCoreApplication::translate("Dpi", preset.name);
    }

    const QList<SiteList> &BlockedSiteLists() {
        static const QList<SiteList> lists = {
            {QStringLiteral("discord"), QT_TRANSLATE_NOOP("Dpi", "Discord"),
             {QStringLiteral("discord.com"), QStringLiteral("discord.gg"), QStringLiteral("discordapp.com"), QStringLiteral("discordapp.net"),
              QStringLiteral("discord.media"), QStringLiteral("discordcdn.com"), QStringLiteral("discord.dev"), QStringLiteral("discord.new"),
              QStringLiteral("discord.gift"), QStringLiteral("discordstatus.com"), QStringLiteral("dis.gd")},
             {}},
            {QStringLiteral("youtube"), QT_TRANSLATE_NOOP("Dpi", "YouTube"), {}, QStringLiteral("list-youtube.txt")},
        };
        return lists;
    }

    const QList<SiteList> &ExcludedSiteLists() {
        static const QList<SiteList> lists = {
            {QStringLiteral("games"), QT_TRANSLATE_NOOP("Dpi", "Game launchers and stores"),
             {QStringLiteral("steampowered.com"), QStringLiteral("steamcommunity.com"), QStringLiteral("steamcontent.com"),
              QStringLiteral("steamserver.net"), QStringLiteral("epicgames.com"), QStringLiteral("riotgames.com"),
              QStringLiteral("leagueoflegends.com"), QStringLiteral("battle.net"), QStringLiteral("blizzard.com"), QStringLiteral("ea.com"),
              QStringLiteral("ubisoft.com"), QStringLiteral("playstation.net"), QStringLiteral("xboxlive.com"), QStringLiteral("rockstargames.com")},
             {}},
        };
        return lists;
    }

    QString SiteListName(const SiteList &list) {
        return QCoreApplication::translate("Dpi", list.name);
    }

    QStringList SiteListHosts(const SiteList &list) {
        QStringList hosts = list.hosts;
        if (!list.zapretFile.isEmpty()) {
            QFile file(QDir(ZapretDir()).filePath(QStringLiteral("files/") + list.zapretFile));
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                for (const auto &line : QString::fromUtf8(file.readAll()).split('\n')) {
                    const auto host = line.trimmed();
                    if (!host.isEmpty() && !host.startsWith('#') && !hosts.contains(host)) hosts << host;
                }
            }
        }
        return hosts;
    }
} // namespace Dpi
