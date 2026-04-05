#include "companion/core/UplinkSync.h"

#include <chrono>
#include <regex>
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

void UplinkSync::sync(const models::ActivitySnapshot& snapshot, const std::optional<models::DevicePolicy>& policy) {
    auto updatedSnapshot = snapshot;

    if (const auto gatewayIpv4 = gatewayHostIpv4(); gatewayIpv4.has_value()) {
        if (m_networkConfigurationAdapter.ensureAirGateway(*gatewayIpv4, {})) {
            m_status = "gateway enforced to " + *gatewayIpv4;
        } else {
            m_status = "gateway enforcement failed";
        }
    } else {
        (void) policy;
    }

    updatedSnapshot.networkIdentity = m_networkConfigurationAdapter.currentIdentity();

    const auto now = std::chrono::steady_clock::now();

    if (shouldSendHeartbeat(now)) {
        if (m_apiClient.sendHeartbeat(m_deviceToken, m_identity, updatedSnapshot, m_networkConfigurationAdapter.describeState())) {
            m_lastHeartbeatAt = now;
            m_hasHeartbeat = true;
            m_status = "heartbeat ok; " + m_networkConfigurationAdapter.describeState();
        } else {
            m_status = "heartbeat failed";
        }
    }

    if (shouldSendActivity(now)) {
        const bool focusedSent = m_apiClient.sendActivity(m_deviceToken, updatedSnapshot);
        if (focusedSent) {
            m_lastActivityAt = now;
            m_hasActivity = true;
            m_status = "activity ok; " + m_networkConfigurationAdapter.describeState();
        } else {
            m_status = "activity failed";
        }
    }

    if (shouldSendInstalledApps(now)) {
        if (m_apiClient.sendInstalledApps(m_deviceToken, updatedSnapshot.installedApps)) {
            m_lastInstalledAppsAt = now;
            m_hasInstalledApps = true;
            m_status = "installed apps ok; " + m_networkConfigurationAdapter.describeState();
        } else {
            m_status = "installed apps failed";
        }
    }
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

std::string UplinkSync::statusSummary() const {
    return m_status;
}

bool UplinkSync::shouldSendHeartbeat(std::chrono::steady_clock::time_point now) const {
    return !m_hasHeartbeat || (now - m_lastHeartbeatAt) >= kHeartbeatInterval;
}

bool UplinkSync::shouldSendActivity(std::chrono::steady_clock::time_point now) const {
    return !m_hasActivity || (now - m_lastActivityAt) >= kActivityInterval;
}

bool UplinkSync::shouldSendInstalledApps(std::chrono::steady_clock::time_point now) const {
    return !m_hasInstalledApps || (now - m_lastInstalledAppsAt) >= kInstalledAppsInterval;
}

std::optional<std::string> UplinkSync::gatewayHostIpv4() const {
    static const std::regex kBaseUrlPattern(R"(^https?://([^/:]+))", std::regex::icase);
    static const std::regex kIpv4Pattern(R"(^(\d{1,3}\.){3}\d{1,3}$)");

    std::smatch match;
    const auto& baseUrl = m_apiClient.baseUrl();
    if (!std::regex_search(baseUrl, match, kBaseUrlPattern) || match.size() < 2) {
        return std::nullopt;
    }

    const std::string host = match[1].str();
    if (!std::regex_match(host, kIpv4Pattern)) {
        return std::nullopt;
    }

    return host;
}

}  // namespace companion::core
