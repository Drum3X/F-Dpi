// F-Dpi: the DPI Bypass engine inside ConnectionManager. A DPI profile is the running profile like any proxy profile, so the
// single-running-profile rule of start()/stop() keeps the proxy (ThroneCore) and winws from ever running together: starting
// one switches away from the other first. The core process itself stays up and idle while winws runs.
#include "include/engine/connection/ConnectionManager.h"

#include "include/configs/outbounds/dpi.h"
#include "include/database/DatabaseManager.h"
#include "include/database/ProfilesRepo.h"
#include "include/dpi/DpiLaunch.h"
#include "include/engine/EnginePolicy.h"
#include "include/engine/EngineTr.h"
#include "include/engine/ThreadCheck.h"
#include "include/engine/EngineHost.h"
#include "include/engine/autoselector/AutoSelectorController.h"
#include "include/engine/dns/AutoDnsService.h"
#include "include/engine/dpi/DpiSupervisor.h"
#include "include/engine/vpn/VpnAuthController.h"
#include "include/global/Logger.hpp"
#include "include/global/RunningProfiles.hpp"
#include "include/global/Utils.hpp"
#include "include/sys/KillSwitch.hpp"

#include <QCoreApplication>

namespace Engine {
    bool ConnectionManager::IsDpiProfile(const std::shared_ptr<Configs::Profile> &profile) {
        return profile != nullptr && profile->outbound != nullptr && profile->outbound->IsDpi();
    }

    ActiveEngine ConnectionManager::activeEngine() const {
        const auto running = runningSnapshot();
        if (running == nullptr) return ActiveEngine::None;
        return IsDpiProfile(running) ? ActiveEngine::Dpi : ActiveEngine::Proxy;
    }

    void ConnectionManager::startDpi(const StartRequest &resolved, const std::shared_ptr<Configs::Profile> &ent) {
        ENGINE_CHECK_MAIN_THREAD();
        const auto title = QCoreApplication::translate("Dpi", "DPI Bypass");
        // The kill switch blocks whatever does not leave through the core, which is everything while winws runs.
        if (Sys::KillSwitch::instance()->state() != Sys::KillSwitch::State::Disabled) {
            failStart(resolved, StartOutcome::StartFailed, title,
                      QCoreApplication::translate("Dpi", "DPI Bypass cannot run while the kill switch is on: it would block all traffic. "
                                                         "Turn the kill switch off first."));
            return;
        }

        if constexpr (DpiSupervisor::kRunsInCore) {
            // Linux: the core runs nfqws and its nftables rule, which needs root.
            if (!Configs::dataManager->settingsRepo->disable_privilege_req && !tunPrivileged()) {
                StartRequest quiet = resolved;
                if (resolved.interactive) {
                    policy()->requestCorePrivileges();
                    quiet.interactive = false;
                }
                failStart(quiet, StartOutcome::StartFailed, title,
                          QCoreApplication::translate("Dpi", "DPI Bypass needs the core to have root privileges. Give the core root "
                                                             "privileges, then start the profile again."));
                return;
            }
        }

        if (!mu_starting.tryLock()) {
            failStart(resolved, StartOutcome::Busy, software_name, MainWindowTr::tr("Another profile is starting..."));
            return;
        }
        if (!mu_stopping.tryLock()) {
            mu_starting.unlock();
            failStart(resolved, StartOutcome::Busy, software_name, MainWindowTr::tr("Another profile is stopping..."));
            return;
        }
        mu_stopping.unlock();

        const Engine::SlowOperation slowStart = resolved.interactive
            ? policy()->slowOperation(Engine::SlowOperationKind::Start, 10000)
            : Engine::SlowOperation{};
        m_profileConnecting = true;
        syncState();
        emit busyChanged();

        runOnNewThread([=, this] {
            const bool switching = runningSnapshot() != nullptr;
            if (switching) {
                m_switchingProfile = true;
                stop({false, false, true, resolved.interactive});
                mu_stopping.lock();
                mu_stopping.unlock();
                m_switchingProfile = false;
            }
            MW_show_log(">>>>>>>> " + MainWindowTr::tr("Starting profile %1").arg(ent->outbound->DisplayTypeAndName()));

            QString error;
            Dpi::Launch launch;
            StartOutcome outcome = StartOutcome::Started;
            if (!Dpi::BuildLaunch(*ent->Dpi(), ent->id, &launch, &error)) {
                outcome = StartOutcome::BuildFailed;
            } else if (!m_dpi->start(launch, &error)) {
                outcome = StartOutcome::StartFailed;
            }
            const bool started = outcome == StartOutcome::Started;
            if (started) {
                // The system proxy points at the core's inbound, which serves nothing while winws runs.
                if (Configs::dataManager->settingsRepo->spmode_system_proxy) {
                    applySystemProxy(false);
                    MW_show_log(QCoreApplication::translate("Dpi", "[DPI] The system proxy is not used while DPI Bypass runs."));
                }
                // winws leaves DNS alone; Otomatik DNS keeps a poisoning ISP resolver out of the way meanwhile.
                if (const auto *engineHost = host(); engineHost != nullptr && engineHost->autoDns() != nullptr) engineHost->autoDns()->dpiStarted();
                Configs::dataManager->settingsRepo->UpdateStartedId(ent->id);
                Configs::SetRunningProfiles({ent->id});
                setRunning(ent, false);
            } else {
                MW_show_log("<<<<<<<< " + MainWindowTr::tr("Failed to start profile %1").arg(ent->outbound->DisplayTypeAndName()));
                MW_show_log(error);
            }
            const auto markerAt = MonotonicMs();
            mu_starting.unlock();

            runOnUiThread([=, this] {
                slowStart.dismiss();
                m_profileConnecting = false;
                syncState();
                emit busyChanged();
                if (started) {
                    m_geoRetriedProfile = -1;
                    clearRestartNeeded();
                    m_vpnAuth->noteRunStarted();
                    emit statusChanged();
                    emit profileRowsChanged({ent->id});
                    emit runMarker(markerAt, switching ? RunMarker::Switched : RunMarker::Started,
                                   ent->outbound ? ent->outbound->DisplayName() : ent->name);
                    emit startFinished(resolved.serial, resolved.profileId, StartOutcome::Started, {});
                    return;
                }
                if (switching) emit runMarker(markerAt, RunMarker::Stopped, {});
                emit startFinished(resolved.serial, resolved.profileId, outcome, error);
                if (resolved.interactive) policy()->startFailed(resolved, outcome, title, error);
            });
        });
    }

    void ConnectionManager::setExclusiveTask(const QString &reason) {
        ENGINE_CHECK_MAIN_THREAD();
        m_exclusiveTask = reason;
    }

    bool ConnectionManager::refuseForExclusiveTask(const StartRequest &request) {
        if (m_exclusiveTask.isEmpty()) return false;
        failStart(request, StartOutcome::Busy, software_name, m_exclusiveTask);
        return true;
    }

    void ConnectionManager::onDpiExited(const QString &reason) {
        ENGINE_CHECK_MAIN_THREAD();
        const auto running = runningSnapshot();
        if (!IsDpiProfile(running)) return;
#ifdef Q_OS_WIN
        MW_show_log(QCoreApplication::translate("Dpi", "[DPI] DPI Bypass stopped because winws exited: %1").arg(reason));
#else
        MW_show_log(QCoreApplication::translate("Dpi", "[DPI] DPI Bypass stopped because nfqws exited: %1").arg(reason));
#endif
        // Not manual: the profile stays the started one, so the next launch tries it again.
        stop({false, false, false, false});
    }
} // namespace Engine
