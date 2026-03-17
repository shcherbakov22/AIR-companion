#include "companion/core/Agent.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <utility>

namespace companion::core {

namespace {

void appendDebugLog(const std::string& line) {
#ifdef _WIN32
    const char* appData = std::getenv("APPDATA");
    if (appData == nullptr || *appData == '\0') {
        return;
    }

    const auto logDirectory = std::filesystem::path(appData) / "AIRCompanion";
    std::error_code errorCode;
    std::filesystem::create_directories(logDirectory, errorCode);

    std::ofstream output(logDirectory / "debug.log", std::ios::app);
    if (!output.is_open()) {
        return;
    }

    output << line << '\n';
#else
    (void) line;
#endif
}

}  // namespace

Agent::Agent(PolicySync policySync,
             CommandPoller commandPoller,
             CaptureScheduler captureScheduler,
             EnforcementCoordinator enforcementCoordinator,
             UplinkSync uplinkSync,
             adapters::IAppTrackerAdapter& appTrackerAdapter,
             adapters::IBrowserDomainAdapter& browserDomainAdapter,
             adapters::INetworkConfigurationAdapter& networkConfigurationAdapter,
             adapters::IScreenCaptureAdapter& screenCaptureAdapter,
             adapters::ICameraCaptureAdapter& cameraCaptureAdapter)
    : m_policySync(std::move(policySync)),
      m_commandPoller(std::move(commandPoller)),
      m_captureScheduler(std::move(captureScheduler)),
      m_enforcementCoordinator(std::move(enforcementCoordinator)),
      m_uplinkSync(std::move(uplinkSync)),
      m_appTrackerAdapter(appTrackerAdapter),
      m_browserDomainAdapter(browserDomainAdapter),
      m_networkConfigurationAdapter(networkConfigurationAdapter),
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

    const auto snapshot = currentSnapshot();
    auto policy = m_policySync.refresh();
    if (policy.has_value()) {
        m_captureScheduler.updatePolicy(*policy);
        m_enforcementCoordinator.applyPolicy(*policy);
        m_lastPolicy = *policy;
        m_status = "policy synced: " + policy->policyHash;
    }

    m_uplinkSync.sync(snapshot, policy.has_value() ? policy : m_lastPolicy);

    for (const auto& command : m_commandPoller.poll()) {
        appendDebugLog("agent command received id=" + command.id);
        m_commandPoller.acknowledge(command.id);
        bool success = true;
        std::string output = "completed";

        switch (command.type) {
            case models::DeviceCommandType::RequestScreenshot: {
                const auto path = m_screenCaptureAdapter.captureToFile(m_captureScheduler.settings().screenOutputDirectory);
                success = path.has_value()
                    && m_uplinkSync.uploadScreenCapture(*path, snapshot, m_captureScheduler.settings().screenContentType);
                output = success ? "screen capture uploaded" : "screen capture failed";
                appendDebugLog("agent screenshot command success=" + std::string(success ? "true" : "false"));
                break;
            }
            case models::DeviceCommandType::RequestCameraCapture: {
                const auto path = m_cameraCaptureAdapter.captureToFile(m_captureScheduler.settings().cameraOutputDirectory);
                success = path.has_value()
                    && m_uplinkSync.uploadCameraCapture(*path, snapshot, m_captureScheduler.settings().cameraContentType);
                output = success ? "camera capture uploaded" : "camera capture failed";
                appendDebugLog("agent camera command success=" + std::string(success ? "true" : "false") + " path=" + (path.has_value() ? *path : std::string{}));
                break;
            }
            default:
                m_enforcementCoordinator.applyCommand(command);
                appendDebugLog("agent non-capture command processed");
                break;
        }

        m_commandPoller.submitResult(command.id, success, output);
    }

    const auto now = std::chrono::steady_clock::now();
    if (m_captureScheduler.shouldCaptureScreen(now)) {
        const auto path = m_screenCaptureAdapter.captureToFile(m_captureScheduler.settings().screenOutputDirectory);
        if (path.has_value()) {
            (void)m_uplinkSync.uploadScreenCapture(*path, snapshot, m_captureScheduler.settings().screenContentType);
            m_captureScheduler.markScreenCaptured(now);
        }
    }

    if (m_captureScheduler.shouldCaptureCamera(now)) {
        const auto path = m_cameraCaptureAdapter.captureToFile(m_captureScheduler.settings().cameraOutputDirectory);
        if (path.has_value()) {
            (void)m_uplinkSync.uploadCameraCapture(*path, snapshot, m_captureScheduler.settings().cameraContentType);
            m_captureScheduler.markCameraCaptured(now);
        }
    }

    m_status += " | " + m_uplinkSync.statusSummary();
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
    snapshot.networkIdentity = m_networkConfigurationAdapter.currentIdentity();

    return snapshot;
}

}  // namespace companion::core
