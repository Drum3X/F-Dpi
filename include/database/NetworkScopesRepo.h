#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <QList>
#include <QString>
#include <QStringList>

#include "Database.h"

namespace Configs {
    class Group;

    // F-Dpi: a network a group is bound to: every id Sys::NetworkMonitor reported for it (NetworkIdentity::keys).
    struct BoundNetwork {
        QStringList keys;
        QString name;

        [[nodiscard]] bool matches(const QStringList &other) const {
            for (const auto &key : other) {
                if (keys.contains(key)) return true;
            }
            return false;
        }
    };

    // F-Dpi: a group is general (the default) or network-specific ("Özel (Ağ Bazlı)"). Groups without a row are general.
    struct GroupScope {
        int groupId = -1;
        bool network = false;
        QList<BoundNetwork> networks;
        // The profile to run on those networks; -1 or a profile no longer in the group: the group's first profile.
        int profileId = -1;
    };

    // Kept in its own table (network_scopes) so the groups table stays Throne's. Rows of deleted groups are ignored: group ids
    // are never reused. Thread-safe.
    class NetworkScopesRepo {
    public:
        explicit NetworkScopesRepo(Database &database);

        GroupScope Get(int groupId);

        void Save(const GroupScope &scope);

        // Network-scoped groups that still exist, in tab order.
        QList<GroupScope> NetworkScopes();

        // The network-scoped group bound to a network with any of these ids; groupId -1 when none.
        GroupScope ForNetwork(const QStringList &keys);

        // A dialog edits a group that has no id until its caller adds it; the scope is saved once it has one (any later call).
        void SetPending(const std::shared_ptr<Group> &group, const GroupScope &scope);

    private:
        void createTables() const;
        void flushPendingLocked();
        GroupScope loadLocked(int groupId) const;
        void saveLocked(const GroupScope &scope);

        Database &db;
        std::recursive_mutex mutex;
        std::vector<std::pair<std::weak_ptr<Group>, GroupScope>> pending;
    };
} // namespace Configs
