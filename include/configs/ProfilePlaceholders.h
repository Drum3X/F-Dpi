#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace Configs {
    class Profile;

    // F-Dpi: network placeholders (include/sys/NetworkPlaceholders.h) in proxy profiles.
    //
    // Only a profile written on this computer expands them. One that came by link, subscription, the Remote API or a
    // backup keeps %LOCAL_IP% and the rest as plain text, so a remote profile cannot make F-Dpi send the computer's address
    // or adapter name to a server. Saving a profile in the editor records a fingerprint of its placeholder texts (settings
    // network_placeholder_trust, host-only); a profile expands while its texts still match. A per-profile check of where
    // the profile came from was not enough: an update by subscription or API rewrites a profile the editor saved, and the
    // fingerprint no longer matches then. DPI profiles are always local (RefusedForeignType("dpi")) and need no trust.
    namespace Placeholders {
        // The profile's own strings that name a placeholder (its export JSON, dial fields included), sorted.
        QStringList Texts(const Profile &profile);

        // Main thread: the editor saved the profile; remembers its placeholder texts (or forgets it when it has none).
        void Trust(const Profile &profile);

        // Whether the profile's placeholders expand.
        bool Trusted(const Profile &profile);

        // Any thread, on a hop's built outbound (sing-box or Xray JSON): fills in every placeholder of a trusted profile.
        // False with *error when a value is missing on this network. An untrusted profile is left as it is (true).
        bool ExpandOutbound(const Profile &profile, QJsonObject *object, QString *error);

        // The placeholder names a running profile depends on: a proxy's own (chain hops included, trusted ones only) and
        // the direct DNS server's, a DPI profile's strategy.
        QStringList Used(const Profile &profile);
    } // namespace Placeholders
} // namespace Configs
