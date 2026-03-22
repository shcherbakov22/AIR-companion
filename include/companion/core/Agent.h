#pragma once

#include <chrono>
#include <string>
#include <optional>

#include "companion/adapters/IBrowserDomainAdapter.h"
#include "companion/adapters/ICameraCaptureAdapter.h"
#include "companion/adapters/IAppTrackerAdapter.h"
#include "companion/adapters/INetworkConfigurationAdapter.h"
#include "companion/adapters/IRemoteAccessAdapter.h"
#include "companion/adapters/IScreenCaptureAdapter.h"
#include "companion/core/CaptureScheduler.h"
#include "companion/core/CommandPoller.h"
#include "companion/core/EnforcementCoordinator.h"
#include "companion/core/PolicySync.h"
#include "companion/core/UplinkSync.h"
#include "companion/service/UpdateCoordinator.h"

namespace companion::core {

class Agent {
public:
    Agent(PolicySync policySync,
          CommandPoller commandPoller,
          CaptureScheduler captureScheduler,
          EnforcementCoordinator enforcementCoordinator,
          UplinkSync uplinkSync,
          service::UpdateCoordinator updateCoordinator,
          adapters::IAppTrackerAdapter& appTrackerAdapter,
          adapters::IBrowserDomainAdapter& browserDomainAdapter,
          adapters::INetworkConfigurationAdapter& networkConfigurationAdapter,
          adapters::IRemoteAccessAdapter& remoteAccessAdapter,
          adapters::IScreenCaptureAdapter& screenCaptureAdapter,
          adapters::ICameraCaptureAdapter& cameraCaptureAdapter);

    void start();
    void stop();
    void tick();
    bool running() const;
    std::string statusSummary() const;

private:
    models::ActivitySnapshot currentSnapshot() const;

    PolicySync m_policySync;
    CommandPoller m_commandPoller;
    CaptureScheduler m_captureScheduler;
    EnforcementCoordinator m_enforcementCoordinator;
    UplinkSync m_uplinkSync;
    service::UpdateCoordinator m_updateCoordinator;
    adapters::IAppTrackerAdapter& m_appTrackerAdapter;
    adapters::IBrowserDomainAdapter& m_browserDomainAdapter;
    adapters::INetworkConfigurationAdapter& m_networkConfigurationAdapter;
    adapters::IRemoteAccessAdapter& m_remoteAccessAdapter;
    adapters::IScreenCaptureAdapter& m_screenCaptureAdapter;
    adapters::ICameraCaptureAdapter& m_cameraCaptureAdapter;
    std::optional<models::DevicePolicy> m_lastPolicy;
    std::vector<models::InstalledAppEntry> m_cachedInstalledApps;
    std::chrono::steady_clock::time_point m_lastInstalledAppsCollectedAt{};
    bool m_hasInstalledAppsCache{false};
    bool m_running{false};
    std::string m_status{"idle"};
};

}  // namespace companion::core
