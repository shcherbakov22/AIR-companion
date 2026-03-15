#pragma once

#include <string>

#include "companion/adapters/IBrowserDomainAdapter.h"
#include "companion/adapters/ICameraCaptureAdapter.h"
#include "companion/adapters/IAppTrackerAdapter.h"
#include "companion/adapters/IScreenCaptureAdapter.h"
#include "companion/core/CaptureScheduler.h"
#include "companion/core/CommandPoller.h"
#include "companion/core/EnforcementCoordinator.h"
#include "companion/core/PolicySync.h"

namespace companion::core {

class Agent {
public:
    Agent(PolicySync policySync,
          CommandPoller commandPoller,
          CaptureScheduler captureScheduler,
          EnforcementCoordinator enforcementCoordinator,
          adapters::IAppTrackerAdapter& appTrackerAdapter,
          adapters::IBrowserDomainAdapter& browserDomainAdapter,
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
    adapters::IAppTrackerAdapter& m_appTrackerAdapter;
    adapters::IBrowserDomainAdapter& m_browserDomainAdapter;
    adapters::IScreenCaptureAdapter& m_screenCaptureAdapter;
    adapters::ICameraCaptureAdapter& m_cameraCaptureAdapter;
    bool m_running{false};
    std::string m_status{"idle"};
};

}  // namespace companion::core

