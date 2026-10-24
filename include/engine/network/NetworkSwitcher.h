#pragma once

#include <QObject>
#include <QString>

#include "include/engine/EngineTypes.h"

namespace Sys {
    struct NetworkIdentity;
}

namespace Engine {
    class ConnectionManager;

    // F-Dpi: runs the config assigned to the current network.
    // - A network-scoped ("Özel") group bound to the network: its chosen profile (or its first one) runs.
    // - Otherwise the general config runs: settings general_profile_id, else the first profile of the first general group.
    // It acts only while the user wants something running (a profile runs, or one is started and not stopped by hand), so it
    // never starts a connection the user turned off. Every start goes through ConnectionManager, which stops the other engine.
    // It also learns: a profile the user starts from a general group becomes the general config, and one started from a
    // network group while on its network becomes that network's config.
    class NetworkSwitcher final : public QObject {
        Q_OBJECT

    public:
        explicit NetworkSwitcher(ConnectionManager *connection, QObject *parent = nullptr);

        // Main thread, before the core starts: detects the network once and returns the profile to start with the core (-1:
        // none). With network_start_on_launch that is the one this network calls for, remembered or not; else the remembered
        // profile resumes as in Throne, swapped for this network's config under network_auto_switch. Then follows
        // Sys::NetworkMonitor.
        int start();

        // Main thread. The profile the current network calls for, given the one running (or about to resume): the bound group's
        // profile; else current itself when it is a general profile (any general config is fine on an unbound network);
        // else the general config. -1 when none.
        int targetProfile(int current) const;

    private:
        void onNetworkChanged(const Sys::NetworkIdentity &network);
        void onStartFinished(quint64 serial, int profileId, Engine::StartOutcome outcome);
        void apply(const QString &why);

        ConnectionManager *m_connection;
        QString m_lastKey;
        bool m_started = false;
    };
} // namespace Engine
