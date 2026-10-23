#include "include/sys/ChildProcess.hpp"

#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <iterator>
#include <system_error>

#include <windows.h>
#include <tlhelp32.h>

namespace Sys {
    namespace {
        // Never closed: the kernel closes it when this process exits, which kills every process in the job.
        HANDLE childProcessJob() {
            static QMutex mutex;
            static HANDLE job = nullptr;
            QMutexLocker locker(&mutex);
            if (job != nullptr) return job;
            HANDLE created = CreateJobObjectW(nullptr, nullptr);
            if (created == nullptr) return nullptr;
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(created, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
                CloseHandle(created);
                return nullptr;
            }
            job = created;
            return job;
        }

        QString childProcessLastError() {
            return QString::fromStdString(std::system_category().message(static_cast<int>(GetLastError()))).trimmed();
        }

        QString childProcessImagePath(HANDLE process) {
            wchar_t buffer[MAX_PATH * 2];
            DWORD size = static_cast<DWORD>(std::size(buffer));
            if (!QueryFullProcessImageNameW(process, 0, buffer, &size)) return {};
            return QString::fromWCharArray(buffer, static_cast<int>(size));
        }
    } // namespace

    bool TieChildToSelf(qint64 pid, QString *error) {
        HANDLE job = childProcessJob();
        if (job == nullptr) {
            if (error) *error = childProcessLastError();
            return false;
        }
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
        if (process == nullptr) {
            if (error) *error = childProcessLastError();
            return false;
        }
        const bool ok = AssignProcessToJobObject(job, process);
        if (!ok && error) *error = childProcessLastError();
        CloseHandle(process);
        return ok;
    }

    int KillProcessesByPath(const QString &executable) {
        const QString wanted = QDir::toNativeSeparators(QFileInfo(executable).absoluteFilePath());
        const QString name = QFileInfo(executable).fileName();
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;
        int killed = 0;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
            if (QString::fromWCharArray(entry.szExeFile).compare(name, Qt::CaseInsensitive) != 0) continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
            if (process == nullptr) continue;
            if (childProcessImagePath(process).compare(wanted, Qt::CaseInsensitive) == 0 && TerminateProcess(process, 1)) {
                WaitForSingleObject(process, 3000);
                ++killed;
            }
            CloseHandle(process);
        }
        CloseHandle(snapshot);
        return killed;
    }
} // namespace Sys
