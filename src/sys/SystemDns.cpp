#include "include/sys/SystemDns.hpp"

#ifndef Q_OS_WIN
namespace Sys::SystemDns {
    bool Supported() { return false; }

    bool Apply(const Servers &, QJsonObject *, QStringList *, QString *error) {
        if (error) *error = QStringLiteral("not supported on this platform");
        return false;
    }

    bool Restore(const QJsonObject &, QString *) { return true; }

    QStringList EffectiveServers() { return {}; }

    void FlushCache() {}
} // namespace Sys::SystemDns
#endif
