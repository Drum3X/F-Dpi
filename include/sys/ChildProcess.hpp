#pragma once

#include <QString>

namespace Sys {
    // F-Dpi: helpers for helper processes that must never outlive the app (winws holds a WinDivert handle that bends traffic).

    // Windows: puts the process in a job object closed with this process, so it dies even when F-Dpi crashes. False with error
    // otherwise; elsewhere a no-op that returns true.
    bool TieChildToSelf(qint64 pid, QString *error);

    // Kills every running process whose executable is exactly this path (left over from a crashed run). Returns how many.
    int KillProcessesByPath(const QString &executable);

    // Windows: stops the kernel driver service (WinDivert stays loaded after its last user exits and locks its .sys file,
    // which blocks updates and uninstalling) when its binary lies in dir and no process runs from dir. True when it was
    // stopped; *note says why not (empty when it was not loaded). Elsewhere false.
    bool StopDriverLoadedFrom(const QString &service, const QString &dir, QString *note = nullptr);
} // namespace Sys
