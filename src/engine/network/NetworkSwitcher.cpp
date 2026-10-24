#include "include/engine/network/NetworkSwitcher.h"

#include <algorithm>

#include <QCoreApplication>

#include "include/database/DatabaseManager.h"
#include "include/database/GroupsRepo.h"
#include "include/database/NetworkScopesRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/configs/ProfilePlaceholders.h"
#include "include/engine/ThreadCheck.h"
#include "include/engine/connection/ConnectionManager.h"
#include "include/global/Logger.hpp"
#include "include/global/Utils.hpp"
#include "include/sys/NetworkMonitor.hpp"
#include "include/sys/NetworkPlaceholders.h"

namespace Engine {
    namespace {
        // The profile a group runs: preferred when still in the group, else its first profile.
        int networkSwitcherGroupProfile(int groupId, int preferred) {
            const auto group = Configs::dataManager->groupsRepo->GetGroup(groupId);
            if (group == nullptr || group->archive) return -1;
            const auto profiles = group->Profiles();
            if (preferred >= 0 && profiles.contains(preferred)) return preferred;
            for (const int id : profiles) {
                if (Configs::dataManager->profilesRepo->GetProfile(id) != nullptr) return id;
            }
            return -1;
        }

        QString networkSwitcherProfileName(int id) {
            const auto profile = Configs::dataManager->profilesRepo->GetProfile(id);
            if (profile == nullptr || profile->outbound == nullptr) return QString::number(id);
            return profile->outbound->DisplayTypeAndName();
        }
    } // namespace

    NetworkSwitcher::NetworkSwitcher(ConnectionManager *connection, QObject *parent) : QObject(parent), m_connection(connection) {
        connect(m_connection, &ConnectionManager::startFinished, this,
                [this](quint64 serial, int profileId, Engine::StartOutcome outcome, const QString &) {
                    onStartFinished(serial, profileId, outcome);
                });
    }

    int NetworkSwitcher::start() {
        ENGINE_CHECK_MAIN_THREAD();
        if (m_started) return -1;
        m_started = true;
        auto *monitor = Sys::NetworkMonitor::instance();
        monitor->start(true);
        const auto network = monitor->current();
        m_lastKey = network.key;
        connect(monitor, &Sys::NetworkMonitor::networkChanged, this, &NetworkSwitcher::onNetworkChanged);
        if (network.connected()) {
            MW_show_log(QCoreApplication::translate("Network", "[Network] Connected to %1.").arg(network.name));
        }
        auto &settings = Configs::dataManager->settingsRepo;
        const int remembered = settings->remember_enable && settings->remember_id >= 0 ? settings->remember_id : -1;
        if (!settings->network_start_on_launch && (!settings->network_auto_switch || remembered < 0)) return remembered;
        const int target = targetProfile(remembered);
        if (target < 0) return remembered;
        if (target != remembered) {
            MW_show_log(QCoreApplication::translate("Network", "[Network] %1: starting %2.")
                            .arg(network.connected() ? network.name : QCoreApplication::translate("Network", "No network"), networkSwitcherProfileName(target)));
            if (remembered >= 0) settings->remember_id = target;
        }
        return target;
    }

    int NetworkSwitcher::targetProfile(int current) const {
        auto &scopes = Configs::dataManager->networkScopesRepo;
        const auto network = Sys::NetworkMonitor::instance()->current();
        if (network.connected()) {
            if (const auto scope = scopes->ForNetwork(network.keys); scope.groupId >= 0) {
                if (const int id = networkSwitcherGroupProfile(scope.groupId, scope.profileId); id >= 0) return id;
            }
        }
        if (const auto profile = Configs::dataManager->profilesRepo->GetProfile(current); profile != nullptr && !scopes->Get(profile->gid).network) {
            return current;
        }
        const auto &settings = Configs::dataManager->settingsRepo;
        if (const auto profile = Configs::dataManager->profilesRepo->GetProfile(settings->general_profile_id); profile != nullptr) {
            if (!scopes->Get(profile->gid).network) return profile->id;
        }
        for (const int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
            if (scopes->Get(gid).network) continue;
            if (const int id = networkSwitcherGroupProfile(gid, -1); id >= 0) return id;
        }
        return -1;
    }

    void NetworkSwitcher::onNetworkChanged(const Sys::NetworkIdentity &network) {
        if (network.connected()) {
            MW_show_log(QCoreApplication::translate("Network", "[Network] Connected to %1.")
                            .arg(network.kind == Sys::NetworkIdentity::Kind::WiFi
                                     ? QCoreApplication::translate("Network", "Wi-Fi \"%1\"").arg(network.name)
                                     : QCoreApplication::translate("Network", "Ethernet %1").arg(network.name)));
        } else {
            MW_show_log(QCoreApplication::translate("Network", "[Network] No network connection."));
        }
        // Offline: nothing to match, and the next network decides.
        if (!network.connected()) return;
        if (network.key != m_lastKey) {
            m_lastKey = network.key;
            if (apply(network.name)) return;
        }
        restartForPlaceholders(network.name);
    }

    bool NetworkSwitcher::apply(const QString &why) {
        const auto &settings = Configs::dataManager->settingsRepo;
        if (!settings->network_auto_switch || settings->prepare_exit) return false;
        // A manual stop clears started_id: the user turned the connection off, so a network change must not turn it on.
        const auto running = m_connection->runningSnapshot();
        if (running == nullptr && settings->started_id < 0) return false;
        const int target = targetProfile(running != nullptr ? running->id : settings->started_id);
        if (target < 0 || (running != nullptr && running->id == target)) return false;
        if (m_connection->connectionState() == ConnectionState::Connecting) return false;
        MW_show_log(QCoreApplication::translate("Network", "[Network] %1: starting %2.").arg(why, networkSwitcherProfileName(target)));
        m_connection->start(StartRequest{target, false, m_connection->nextStartSerial()});
        return true;
    }

    void NetworkSwitcher::restartForPlaceholders(const QString &why) {
        if (Configs::dataManager->settingsRepo->prepare_exit || m_placeholderValues.isEmpty()) return;
        const auto running = m_connection->runningSnapshot();
        if (running == nullptr || running->id != m_placeholderProfile) return;
        if (m_connection->connectionState() == ConnectionState::Connecting) return;
        const auto now = Sys::NetworkPlaceholderValues();
        QStringList changed;
        for (auto it = m_placeholderValues.cbegin(); it != m_placeholderValues.cend(); ++it) {
            if (now.value(it.key()) != it.value()) changed << QLatin1Char('%') + it.key() + QLatin1Char('%');
        }
        if (changed.isEmpty()) return;
        MW_show_log(QCoreApplication::translate("Network", "[Network] %1: %2 changed, restarting %3.")
                        .arg(why, changed.join(QStringLiteral(", ")), networkSwitcherProfileName(running->id)));
        m_connection->start(StartRequest{running->id, false, m_connection->nextStartSerial()});
    }

    void NetworkSwitcher::onStartFinished(quint64, int profileId, Engine::StartOutcome outcome) {
        if (outcome != StartOutcome::Started) return;
        const auto profile = Configs::dataManager->profilesRepo->GetProfile(profileId);
        if (profile == nullptr) return;
        // The placeholder values this start used, to notice when they change.
        m_placeholderProfile = profileId;
        m_placeholderValues.clear();
        const auto values = Sys::NetworkPlaceholderValues();
        for (const auto &name : Configs::Placeholders::Used(*profile)) m_placeholderValues.insert(name, values.value(name));
        auto &scopes = Configs::dataManager->networkScopesRepo;
        auto scope = scopes->Get(profile->gid);
        if (!scope.network) {
            auto &settings = Configs::dataManager->settingsRepo;
            if (settings->general_profile_id != profileId) {
                settings->general_profile_id = profileId;
                settings->Save();
            }
            return;
        }
        const auto network = Sys::NetworkMonitor::instance()->current();
        const bool onThisNetwork = network.connected() && std::any_of(scope.networks.begin(), scope.networks.end(), [&](const auto &bound) {
            return bound.matches(network.keys);
        });
        if (onThisNetwork && scope.profileId != profileId) {
            scope.profileId = profileId;
            scopes->Save(scope);
        }
    }
} // namespace Engine
