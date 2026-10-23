#include "include/database/NetworkScopesRepo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "include/database/GroupsRepo.h"
#include "include/global/Configs.hpp"

namespace Configs {
    namespace {
        QString networkScopesToJson(const QList<BoundNetwork> &networks) {
            QJsonArray array;
            for (const auto &network : networks) array.append(QJsonObject{{"keys", QJsonArray::fromStringList(network.keys)}, {"name", network.name}});
            return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
        }

        QList<BoundNetwork> networkScopesFromJson(const std::string &text) {
            QList<BoundNetwork> networks;
            for (const auto &value : QJsonDocument::fromJson(QByteArray::fromStdString(text)).array()) {
                const auto object = value.toObject();
                BoundNetwork network;
                for (const auto &key : object["keys"].toArray()) {
                    if (!key.toString().isEmpty()) network.keys << key.toString();
                }
                network.name = object["name"].toString();
                if (!network.keys.isEmpty()) networks << network;
            }
            return networks;
        }
    } // namespace

    NetworkScopesRepo::NetworkScopesRepo(Database &database) : db(database) {
        createTables();
    }

    void NetworkScopesRepo::createTables() const {
        db.exec(R"(
            CREATE TABLE IF NOT EXISTS network_scopes (
                group_id INTEGER PRIMARY KEY,
                scope TEXT NOT NULL DEFAULT 'general',
                networks_json TEXT NOT NULL DEFAULT '[]',
                profile_id INTEGER NOT NULL DEFAULT -1
            )
        )");
    }

    GroupScope NetworkScopesRepo::loadLocked(int groupId) const {
        GroupScope scope;
        scope.groupId = groupId;
        auto query = db.query("SELECT scope, networks_json, profile_id FROM network_scopes WHERE group_id = ?", groupId);
        if (query && query->executeStep()) {
            scope.network = query->getColumn(0).getText() == std::string("network");
            scope.networks = networkScopesFromJson(query->getColumn(1).getText());
            scope.profileId = query->getColumn(2).getInt();
        }
        return scope;
    }

    void NetworkScopesRepo::saveLocked(const GroupScope &scope) {
        if (scope.groupId < 0) return;
        db.exec(R"(
            INSERT INTO network_scopes (group_id, scope, networks_json, profile_id) VALUES (?, ?, ?, ?)
            ON CONFLICT(group_id) DO UPDATE SET scope = excluded.scope, networks_json = excluded.networks_json,
                profile_id = excluded.profile_id
        )",
                scope.groupId, std::string(scope.network ? "network" : "general"), networkScopesToJson(scope.networks).toStdString(),
                scope.profileId);
    }

    void NetworkScopesRepo::flushPendingLocked() {
        for (auto it = pending.begin(); it != pending.end();) {
            const auto group = it->first.lock();
            if (group == nullptr) {
                it = pending.erase(it);
                continue;
            }
            if (group->id < 0) {
                ++it;
                continue;
            }
            auto scope = it->second;
            scope.groupId = group->id;
            saveLocked(scope);
            it = pending.erase(it);
        }
    }

    GroupScope NetworkScopesRepo::Get(int groupId) {
        std::lock_guard locker(mutex);
        flushPendingLocked();
        return loadLocked(groupId);
    }

    void NetworkScopesRepo::Save(const GroupScope &scope) {
        std::lock_guard locker(mutex);
        saveLocked(scope);
    }

    QList<GroupScope> NetworkScopesRepo::NetworkScopes() {
        std::lock_guard locker(mutex);
        flushPendingLocked();
        QList<GroupScope> scopes;
        for (const int gid : dataManager->groupsRepo->GetGroupsTabOrder()) {
            auto scope = loadLocked(gid);
            if (scope.network) scopes << scope;
        }
        return scopes;
    }

    GroupScope NetworkScopesRepo::ForNetwork(const QStringList &keys) {
        for (const auto &scope : NetworkScopes()) {
            const auto group = dataManager->groupsRepo->GetGroup(scope.groupId);
            if (group == nullptr || group->archive) continue;
            for (const auto &network : scope.networks) {
                if (network.matches(keys)) return scope;
            }
        }
        return {};
    }

    void NetworkScopesRepo::SetPending(const std::shared_ptr<Group> &group, const GroupScope &scope) {
        std::lock_guard locker(mutex);
        if (group->id >= 0) {
            auto saved = scope;
            saved.groupId = group->id;
            saveLocked(saved);
            return;
        }
        for (auto &entry : pending) {
            if (entry.first.lock() == group) {
                entry.second = scope;
                return;
            }
        }
        pending.emplace_back(group, scope);
    }
} // namespace Configs
