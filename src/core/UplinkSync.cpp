#include "companion/core/UplinkSync.h"
#include "companion/support/LocalLog.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <utility>

namespace companion::core {

namespace {

constexpr auto kHeartbeatInterval = std::chrono::seconds(10);
constexpr auto kActivityInterval = std::chrono::seconds(30);
constexpr auto kInstalledAppsInterval = std::chrono::hours(12);

}  // namespace

UplinkSync::UplinkSync(networking::CompanionApiClient apiClient,
                       std::string deviceToken,
                       models::DeviceIdentity identity,
                       adapters::INetworkConfigurationAdapter& networkConfigurationAdapter)
    : m_apiClient(std::move(apiClient)),
      m_deviceToken(std::move(deviceToken)),
      m_identity(std::move(identity)),
      m_networkConfigurationAdapter(networkConfigurationAdapter) {}

bool UplinkSync::sync(const models::ActivitySnapshot& snapshot, const std::optional<models::DevicePolicy>& policy) {
    auto updatedSnapshot = snapshot;
    (void) policy;

    updatedSnapshot.networkIdentity = m_networkConfigurationAdapter.currentIdentity();

    const auto now = std::chrono::steady_clock::now();
    const auto currentOpenAppsSignature = openAppsSignature(updatedSnapshot.openApps);
    bool activityWasSent = false;

    if (shouldSendHeartbeat(now)) {
        if (m_apiClient.sendHeartbeat(m_deviceToken, m_identity, updatedSnapshot, m_networkConfigurationAdapter.describeState())) {
            m_lastHeartbeatAt = now;
            m_hasHeartbeat = true;
            m_status = "heartbeat ok; " + m_networkConfigurationAdapter.describeState();
        } else {
            m_status = "heartbeat failed";
            companion::support::appendDebugLog("uplink heartbeat failed network=" + m_networkConfigurationAdapter.describeState());
        }
    }

    if (shouldSendActivity(now, currentOpenAppsSignature)) {
        const bool focusedSent = m_apiClient.sendActivity(m_deviceToken, updatedSnapshot);
        if (focusedSent) {
            m_lastActivityAt = now;
            m_hasActivity = true;
            m_lastOpenAppsSignature = currentOpenAppsSignature;
            activityWasSent = true;
            m_status = "activity ok; " + m_networkConfigurationAdapter.describeState();
        } else {
            m_status = "activity failed";
            companion::support::appendDebugLog("uplink activity failed open_apps=" + std::to_string(updatedSnapshot.openApps.size()));
        }
    }

    if (shouldSendInstalledApps(now)) {
        if (m_apiClient.sendInstalledApps(m_deviceToken, updatedSnapshot.installedApps)) {
            m_lastInstalledAppsAt = now;
            m_hasInstalledApps = true;
            m_status = "installed apps ok; " + m_networkConfigurationAdapter.describeState();
        } else {
            m_status = "installed apps failed";
            companion::support::appendDebugLog("uplink installed_apps failed count=" + std::to_string(updatedSnapshot.installedApps.size()));
        }
    }

    return activityWasSent;
}

bool UplinkSync::uploadScreenCapture(const std::string& filePath,
                                     const models::ActivitySnapshot& snapshot,
                                     const std::string& contentType) {
    return m_apiClient.uploadScreenCapture(m_deviceToken, filePath, snapshot, contentType);
}

bool UplinkSync::uploadCameraCapture(const std::string& filePath,
                                     const models::ActivitySnapshot& snapshot,
                                     const std::string& contentType) {
    return m_apiClient.uploadCameraCapture(m_deviceToken, filePath, snapshot, contentType);
}

bool UplinkSync::reportAppEnforcementFailures(const std::vector<std::string>& failures) {
    const auto signature = enforcementFailureSignature(failures);
    if (signature.empty()) {
        if (!m_lastEnforcementFailureSignature.empty()) {
            companion::support::appendDebugLog("app enforcement failures cleared");
        }
        m_lastEnforcementFailureSignature.clear();
        return true;
    }

    if (signature == m_lastEnforcementFailureSignature) {
        companion::support::appendDebugLog("app enforcement failure report suppressed duplicate count=" + std::to_string(failures.size()));
        return true;
    }

    if (!m_apiClient.sendAppEnforcementReport(m_deviceToken, failures)) {
        m_status = "app enforcement report failed";
        companion::support::appendDebugLog("app enforcement failure report failed count=" + std::to_string(failures.size()));
        return false;
    }

    m_lastEnforcementFailureSignature = signature;
    m_status = "app enforcement report ok";
    companion::support::appendDebugLog("app enforcement failure report ok count=" + std::to_string(failures.size()));
    return true;
}

std::string UplinkSync::statusSummary() const {
    return m_status;
}

bool UplinkSync::shouldSendHeartbeat(std::chrono::steady_clock::time_point now) const {
    return !m_hasHeartbeat || (now - m_lastHeartbeatAt) >= kHeartbeatInterval;
}

bool UplinkSync::shouldSendActivity(std::chrono::steady_clock::time_point now, const std::string& openAppsSignature) const {
    return !m_hasActivity
        || m_lastOpenAppsSignature != openAppsSignature
        || (now - m_lastActivityAt) >= kActivityInterval;
}

bool UplinkSync::shouldSendInstalledApps(std::chrono::steady_clock::time_point now) const {
    return !m_hasInstalledApps || (now - m_lastInstalledAppsAt) >= kInstalledAppsInterval;
}

std::string UplinkSync::openAppsSignature(const std::vector<models::OpenAppEntry>& openApps) const {
    std::vector<std::string> names;
    names.reserve(openApps.size());

    for (const auto& app : openApps) {
        auto name = app.appName;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        if (!name.empty()) {
            names.push_back(std::move(name));
        }
    }

    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());

    std::ostringstream signature;
    for (const auto& name : names) {
        signature << name << '\n';
    }

    return signature.str();
}

std::string UplinkSync::enforcementFailureSignature(std::vector<std::string> failures) const {
    std::sort(failures.begin(), failures.end());
    failures.erase(std::unique(failures.begin(), failures.end()), failures.end());

    std::ostringstream signature;
    for (const auto& failure : failures) {
        if (!failure.empty()) {
            signature << failure << '\n';
        }
    }

    return signature.str();
}

}  // namespace companion::core
