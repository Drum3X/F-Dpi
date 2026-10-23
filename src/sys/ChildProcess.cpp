#include "include/sys/ChildProcess.hpp"

// Windows: src/sys/windows/ChildProcess_win.cpp.
#ifndef Q_OS_WIN
namespace Sys {
    bool TieChildToSelf(qint64, QString *) { return true; }

    int KillProcessesByPath(const QString &) { return 0; }
} // namespace Sys
#endif
