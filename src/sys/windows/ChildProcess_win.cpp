#include "include/sys/ChildProcess.hpp"

#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <iterator>
#include <system_error>
#include <vector>

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

        // "C:\dir\" with a trailing separator, for prefix checks.
        QString childProcessDirPrefix(const QString &dir) {
            auto native = QDir::toNativeSeparators(QDir(dir).absolutePath());
            if (!native.endsWith(QLatin1Char('\\'))) native += QLatin1Char('\\');
            return native;
        }

        struct ChildProcessServiceHandle {
            SC_HANDLE handle;
            ~ChildProcessServiceHandle() {
                if (handle != nullptr) CloseServiceHandle(handle);
            }
        };

        bool childProcessAnyRunsFrom(const QString &prefix) {
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE) return true;
            bool found = false;
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            for (BOOL more = Process32FirstW(snapshot, &entry); more && !found; more = Process32NextW(snapshot, &entry)) {
                HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                if (process == nullptr) continue;
                found = childProcessImagePath(process).startsWith(prefix, Qt::CaseInsensitive);
                CloseHandle(process);
            }
            CloseHandle(snapshot);
            return found;
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

    bool StopDriverLoadedFrom(const QString &service, const QString &dir, QString *note) {
        const auto setNote = [note](const QString &text) {
            if (note) *note = text;
            return false;
        };
        const ChildProcessServiceHandle manager{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
        if (manager.handle == nullptr) return setNote(childProcessLastError());
        const ChildProcessServiceHandle driver{OpenServiceW(manager.handle, reinterpret_cast<LPCWSTR>(service.utf16()),
                                                            SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_STOP)};
        if (driver.handle == nullptr) return setNote(GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST ? QString() : childProcessLastError());
        SERVICE_STATUS status{};
        if (!QueryServiceStatus(driver.handle, &status)) return setNote(childProcessLastError());
        if (status.dwCurrentState != SERVICE_RUNNING) return setNote({});

        DWORD needed = 0;
        QueryServiceConfigW(driver.handle, nullptr, 0, &needed);
        std::vector<unsigned char> buffer(needed);
        auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
        if (needed == 0 || !QueryServiceConfigW(driver.handle, config, needed, &needed)) return setNote(childProcessLastError());
        // Kernel drivers record an NT path: "\??\C:\...".
        auto binary = QString::fromWCharArray(config->lpBinaryPathName);
        if (binary.startsWith(QStringLiteral("\\??\\"))) binary = binary.mid(4);
        const auto prefix = childProcessDirPrefix(dir);
        if (!binary.startsWith(prefix, Qt::CaseInsensitive)) return setNote(QStringLiteral("loaded from %1, not ours").arg(binary));
        if (childProcessAnyRunsFrom(prefix)) return setNote(QStringLiteral("still in use by a process from %1").arg(prefix));

        if (!ControlService(driver.handle, SERVICE_CONTROL_STOP, &status)) return setNote(childProcessLastError());
        return true;
    }
} // namespace Sys
