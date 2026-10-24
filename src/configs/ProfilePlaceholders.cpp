#include "include/configs/ProfilePlaceholders.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

#include "include/configs/outbounds/dpi.h"
#include "include/database/DatabaseManager.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/database/entities/Profile.h"
#include "include/database/entities/RouteProfile.h"
#include "include/dns/AutoDns.h"
#include "include/sys/NetworkPlaceholders.h"

namespace Configs::Placeholders {
    namespace {
        void profilePlaceholderStrings(const QJsonValue &value, QStringList *out) {
            if (value.isString()) {
                if (Sys::HasNetworkPlaceholders(value.toString())) out->append(value.toString());
            } else if (value.isObject()) {
                for (const auto &item : value.toObject()) profilePlaceholderStrings(item, out);
            } else if (value.isArray()) {
                for (const auto &item : value.toArray()) profilePlaceholderStrings(item, out);
            }
        }

        // Replaces the placeholders in every string below value; false on the first missing value.
        bool profilePlaceholderExpand(QJsonValue *value, QString *error) {
            if (value->isString()) {
                const auto text = value->toString();
                if (!Sys::HasNetworkPlaceholders(text)) return true;
                const auto expanded = Sys::ExpandNetworkPlaceholders(text, error);
                if (expanded.isEmpty()) return false;
                *value = expanded;
            } else if (value->isObject()) {
                auto object = value->toObject();
                for (auto it = object.begin(); it != object.end(); ++it) {
                    QJsonValue item = it.value();
                    if (!profilePlaceholderExpand(&item, error)) return false;
                    it.value() = item;
                }
                *value = object;
            } else if (value->isArray()) {
                auto array = value->toArray();
                for (qsizetype i = 0; i < array.size(); ++i) {
                    QJsonValue item = array.at(i);
                    if (!profilePlaceholderExpand(&item, error)) return false;
                    array.replace(i, item);
                }
                *value = array;
            }
            return true;
        }

        QString profilePlaceholderFingerprint(const QStringList &texts) {
            return QString::fromLatin1(QCryptographicHash::hash(texts.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha1).toHex());
        }

        QJsonObject profilePlaceholderTrustMap() {
            return QJsonDocument::fromJson(Configs::dataManager->settingsRepo->network_placeholder_trust.toUtf8()).object();
        }

        void profilePlaceholderNames(const QString &text, QStringList *names) {
            for (const auto &name : Sys::NetworkPlaceholdersIn(text)) {
                if (!names->contains(name)) names->append(name);
            }
        }
    } // namespace

    QStringList Texts(const Profile &profile) {
        QStringList texts;
        if (profile.outbound == nullptr) return texts;
        profilePlaceholderStrings(profile.outbound->ExportToJson(), &texts);
        texts.sort();
        return texts;
    }

    void Trust(const Profile &profile) {
        if (profile.id < 0 || profile.Dpi() != nullptr) return;
        auto &settings = Configs::dataManager->settingsRepo;
        auto map = profilePlaceholderTrustMap();
        const auto key = QString::number(profile.id);
        const auto texts = Texts(profile);
        if (texts.isEmpty()) {
            if (!map.contains(key)) return;
            map.remove(key);
        } else {
            map[key] = profilePlaceholderFingerprint(texts);
        }
        settings->network_placeholder_trust = QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact));
        settings->Save();
    }

    bool Trusted(const Profile &profile) {
        if (profile.Dpi() != nullptr) return true;
        const auto texts = Texts(profile);
        if (texts.isEmpty()) return true;
        return profilePlaceholderTrustMap().value(QString::number(profile.id)).toString() == profilePlaceholderFingerprint(texts);
    }

    bool ExpandOutbound(const Profile &profile, QJsonObject *object, QString *error) {
        if (!Trusted(profile)) return true;
        QJsonValue value = *object;
        if (!profilePlaceholderExpand(&value, error)) {
            if (error != nullptr && profile.outbound != nullptr && !profile.outbound->name.isEmpty()) {
                *error = profile.outbound->name + QStringLiteral(": ") + *error;
            }
            return false;
        }
        *object = value.toObject();
        return true;
    }

    QStringList Used(const Profile &profile) {
        QStringList names;
        if (const auto *dpi = profile.Dpi(); dpi != nullptr) {
            profilePlaceholderNames(dpi->strategy, &names);
            return names;
        }
        QList<int> hops;
        if (const auto *chain = profile.Chain(); chain != nullptr) hops = chain->list;
        if (Trusted(profile)) {
            for (const auto &text : Texts(profile)) profilePlaceholderNames(text, &names);
        }
        for (const int id : hops) {
            const auto hop = Configs::dataManager->profilesRepo->GetProfile(id);
            if (hop == nullptr || !Trusted(*hop)) continue;
            for (const auto &text : Texts(*hop)) profilePlaceholderNames(text, &names);
        }
        bool routeOwnsDns = false;
        auto dns = RouteDns::Current(&routeOwnsDns);
        Dns::ApplyToRouteDns(&dns, routeOwnsDns);
        profilePlaceholderNames(dns.direct_dns, &names);
        return names;
    }
} // namespace Configs::Placeholders
