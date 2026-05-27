#include "companion/core/Agent.h"
#include "companion/support/LocalLog.h"

#include <chrono>
#include <optional>
#include <sstream>
#include <utility>

namespace companion::core {

namespace {
constexpr auto kInstalledAppsRefreshInterval = std::chrono::minutes(10);
constexpr auto kHotspotEnforcementInterval = std::chrono::seconds(30);

std::optional<std::string> jsonStringValue(const std::string& body, const std::string& key) {
    const auto keyPos = body.find("\"" + key + "\"");
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    const auto colonPos = body.find(':', keyPos);
    if (colonPos == std::string::npos) {
        return std::nullopt;
    }

    const auto valueStart = body.find_first_not_of(" \t\r\n", colonPos + 1);
    if (valueStart == std::string::npos || body[valueStart] != '"') {
        return std::nullopt;
    }

    std::string value;
    for (std::size_t index = valueStart + 1; index < body.size(); ++index) {
        const char ch = body[index];
        if (ch == '\\' && index + 1 < body.size()) {
            value += body[index + 1];
            ++index;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value += ch;
    }

    return std::nullopt;
}

std::string policySummary(const models::DevicePolicy& policy, const models::ActivitySnapshot& snapshot) {
    std::ostringstream out;
    out << "policy hash=" << policy.policyHash
        << " task=" << (policy.activeTaskName.empty() ? "(none)" : policy.activeTaskName)
        << " open_violations=" << (policy.hasOpenViolations ? "true" : "false")
        << " kill_gui_apps=" << (policy.violationAppEnforcement.killGuiApps ? "true" : "false")
        << " browser_grace_seconds=" << policy.violationAppEnforcement.browserReopenGraceSeconds
        << " browser_tracking=" << (policy.browserTrackingEnabled ? "enabled" : "disabled")
        << " blocked_apps=" << policy.blockedApps.size()
        << " open_apps=" << snapshot.openApps.size()
        << " focused_app=" << snapshot.focusedApp
        << " active_domain=" << snapshot.activeBrowserDomain;
    return out.str();
}

}  // namespace

Agent::Agent(PolicySync policySync,
             CommandPoller commandPoller,
             CaptureScheduler captureScheduler,
             EnforcementCoordinator enforcementCoordinator,
             PushUpStationCoordinator pushUpStationCoordinator,
             UplinkSync uplinkSync,
             service::UpdateCoordinator updateCoordinator,
          adapters::IAppTrackerAdapter& appTrackerAdapter,
          adapters::IBrowserDomainAdapter& browserDomainAdapter,
          adapters::INetworkConfigurationAdapter& networkConfigurationAdapter,
          adapters::IRemoteAccessAdapter& remoteAccessAdapter,
          adapters::IScreenCaptureAdapter& screenCaptureAdapter,
          adapters::ICameraCaptureAdapter& cameraCaptureAdapter)
    : m_policySync(std::move(policySync)),
      m_commandPoller(std::move(commandPoller)),
      m_captureScheduler(std::move(captureScheduler)),
      m_enforcementCoordinator(std::move(enforcementCoordinator)),
      m_pushUpStationCoordinator(std::move(pushUpStationCoordinator)),
      m_uplinkSync(std::move(uplinkSync)),
      m_updateCoordinator(std::move(updateCoordinator)),
      m_appTrackerAdapter(appTrackerAdapter),
      m_browserDomainAdapter(browserDomainAdapter),
      m_networkConfigurationAdapter(networkConfigurationAdapter),
      m_remoteAccessAdapter(remoteAccessAdapter),
      m_screenCaptureAdapter(screenCaptureAdapter),
      m_cameraCaptureAdapter(cameraCaptureAdapter) {}

void Agent::start() {
    m_running = true;
    m_status = "running";
    companion::support::appendDebugLog("agent started log_dir=" + companion::support::localLogDirectory());
}

void Agent::stop() {
    m_running = false;
    m_status = "stopped";
    companion::support::appendDebugLog("agent stopped");
}

void Agent::tick() {
    if (!m_running) {
        return;
    }

    m_pushUpStationCoordinator.tick();

    const auto now = std::chrono::steady_clock::now();
    if (m_lastHotspotEnforcedAt.time_since_epoch().count() == 0
        || (now - m_lastHotspotEnforcedAt) >= kHotspotEnforcementInterval) {
        if (!m_networkConfigurationAdapter.enforceHotspotDisabled()) {
            companion::support::appendDebugLog("network hardening failed: hotspot disable enforcement did not fully apply");
        }
        m_lastHotspotEnforcedAt = now;
    }

    if (!m_hasInstalledAppsCache || (now - m_lastInstalledAppsCollectedAt) >= kInstalledAppsRefreshInterval) {
        m_cachedInstalledApps = m_appTrackerAdapter.installedApps();
        m_lastInstalledAppsCollectedAt = now;
        m_hasInstalledAppsCache = true;
    }

    auto snapshot = currentSnapshot();
    snapshot.installedApps = m_cachedInstalledApps;
    auto policy = m_policySync.refresh();
    if (policy.has_value()) {
        if (policy->policyHash != m_lastLoggedPolicyHash) {
            companion::support::appendDebugLog("policy sync: " + policySummary(*policy, snapshot));
            m_lastLoggedPolicyHash = policy->policyHash;
        }
        m_captureScheduler.updatePolicy(*policy);
        m_uplinkSync.reportAppEnforcementFailures(m_enforcementCoordinator.applyPolicy(*policy, snapshot));
        m_lastPolicy = *policy;
        m_status = "policy synced: " + policy->policyHash;
    }

    const bool activityWasSent = m_uplinkSync.sync(snapshot, policy.has_value() ? policy : m_lastPolicy);
    if (activityWasSent) {
        auto refreshedPolicy = m_policySync.refresh();
        if (refreshedPolicy.has_value()) {
            if (refreshedPolicy->policyHash != m_lastLoggedPolicyHash) {
                companion::support::appendDebugLog("policy sync after activity: " + policySummary(*refreshedPolicy, snapshot));
                m_lastLoggedPolicyHash = refreshedPolicy->policyHash;
            }
            m_captureScheduler.updatePolicy(*refreshedPolicy);
            m_uplinkSync.reportAppEnforcementFailures(m_enforcementCoordinator.applyPolicy(*refreshedPolicy, snapshot));
            m_lastPolicy = *refreshedPolicy;
            m_status = "policy synced after activity: " + refreshedPolicy->policyHash;
        }
    }

    for (const auto& command : m_commandPoller.poll()) {
        companion::support::appendDebugLog("agent command received id=" + command.id);
        m_commandPoller.acknowledge(command.id);
        bool success = true;
        std::string output = "completed";

        switch (command.type) {
            case models::DeviceCommandType::RequestScreenshot: {
                const auto path = m_screenCaptureAdapter.captureToFile(m_captureScheduler.settings().screenOutputDirectory);
                if (!path.has_value()) {
                    success = false;
                    output = "screen capture create failed";
                } else if (!m_uplinkSync.uploadScreenCapture(*path, snapshot, m_captureScheduler.settings().screenContentType)) {
                    success = false;
                    output = "screen capture upload failed";
                } else {
                    success = true;
                    output = "screen capture uploaded";
                }
                companion::support::appendDebugLog("agent screenshot command success=" + std::string(success ? "true" : "false"));
                break;
            }
            case models::DeviceCommandType::RequestCameraCapture: {
                const auto path = m_cameraCaptureAdapter.captureToFile(m_captureScheduler.settings().cameraOutputDirectory);
                if (!path.has_value()) {
                    success = false;
                    output = "camera capture create failed";
                } else if (!m_uplinkSync.uploadCameraCapture(*path, snapshot, m_captureScheduler.settings().cameraContentType)) {
                    success = false;
                    output = "camera capture upload failed";
                } else {
                    success = true;
                    output = "camera capture uploaded";
                }
                companion::support::appendDebugLog("agent camera command success=" + std::string(success ? "true" : "false") + " path=" + (path.has_value() ? *path : std::string{}));
                break;
            }
            case models::DeviceCommandType::VerifyRemoteControl: {
                success = m_remoteAccessAdapter.verifyReadiness();
                output = success ? "remote control ready" : m_remoteAccessAdapter.currentState().failureReason;
                break;
            }
            case models::DeviceCommandType::StartRemoteControl: {
                success = m_remoteAccessAdapter.startRemoteControl();
                output = success ? "remote control started" : m_remoteAccessAdapter.currentState().failureReason;
                break;
            }
            case models::DeviceCommandType::StopRemoteControl: {
                success = m_remoteAccessAdapter.stopRemoteControl();
                output = success ? "remote control stopped" : m_remoteAccessAdapter.currentState().failureReason;
                break;
            }
            default: {
                const auto result = m_enforcementCoordinator.applyCommand(command);
                success = result.success;
                output = result.output;
                companion::support::appendDebugLog("agent non-capture command processed");
                break;
            }
        }

        m_commandPoller.submitResult(command.id, success, output);
    }

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

    m_updateCoordinator.tick();

    m_status += " | " + m_uplinkSync.statusSummary()
        + " | " + m_pushUpStationCoordinator.statusSummary()
        + " | " + m_updateCoordinator.statusSummary();
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
    (void) m_remoteAccessAdapter.verifyReadiness();
    snapshot.remoteAccessState = m_remoteAccessAdapter.currentState();

    return snapshot;
}

}  // namespace companion::core
