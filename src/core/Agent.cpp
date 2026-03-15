#include "companion/core/Agent.h"

#include <chrono>
#include <utility>

namespace companion::core {

Agent::Agent(PolicySync policySync,
             CommandPoller commandPoller,
             CaptureScheduler captureScheduler,
             EnforcementCoordinator enforcementCoordinator,
             adapters::IAppTrackerAdapter& appTrackerAdapter,
             adapters::IBrowserDomainAdapter& browserDomainAdapter,
             adapters::IScreenCaptureAdapter& screenCaptureAdapter,
             adapters::ICameraCaptureAdapter& cameraCaptureAdapter)
    : m_policySync(std::move(policySync)),
      m_commandPoller(std::move(commandPoller)),
      m_captureScheduler(std::move(captureScheduler)),
      m_enforcementCoordinator(std::move(enforcementCoordinator)),
      m_appTrackerAdapter(appTrackerAdapter),
      m_browserDomainAdapter(browserDomainAdapter),
      m_screenCaptureAdapter(screenCaptureAdapter),
      m_cameraCaptureAdapter(cameraCaptureAdapter) {}

void Agent::start() {
    m_running = true;
    m_status = "running";
}

void Agent::stop() {
    m_running = false;
    m_status = "stopped";
}

void Agent::tick() {
    if (!m_running) {
        return;
    }

    auto policy = m_policySync.refresh();
    if (policy.has_value()) {
        m_captureScheduler.updatePolicy(*policy);
        m_enforcementCoordinator.applyPolicy(*policy);
        m_status = "policy synced: " + policy->policyHash;
    }

    for (const auto& command : m_commandPoller.poll()) {
        m_commandPoller.acknowledge(command.id);
        m_enforcementCoordinator.applyCommand(command);
        m_commandPoller.submitResult(command.id, true, "stub");
    }

    const auto now = std::chrono::steady_clock::now();
    if (m_captureScheduler.shouldCaptureScreen(now)) {
        (void)m_screenCaptureAdapter.captureToFile("captures/screen");
        m_captureScheduler.markScreenCaptured(now);
    }

    if (m_captureScheduler.shouldCaptureCamera(now)) {
        (void)m_cameraCaptureAdapter.captureToFile("captures/camera");
        m_captureScheduler.markCameraCaptured(now);
    }
}

bool Agent::running() const {
    return m_running;
}

std::string Agent::statusSummary() const {
    return m_status;
}

models::ActivitySnapshot Agent::currentSnapshot() const {
    auto snapshot = m_appTrackerAdapter.snapshot();
    if (const auto domain = m_browserDomainAdapter.activeDomain(); domain.has_value()) {
        snapshot.activeBrowserDomain = *domain;
    }

    return snapshot;
}

}  // namespace companion::core
