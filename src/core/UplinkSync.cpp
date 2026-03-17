#include "companion/core/UplinkSync.h"

#include <chrono>
#include <utility>

namespace companion::core {

namespace {

constexpr auto kHeartbeatInterval = std::chrono::seconds(10);
constexpr auto kActivityInterval = std::chrono::seconds(30);

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
    (void) policy;

    updatedSnapshot.networkIdentity = m_networkConfigurationAdapter.currentIdentity();

    const auto now = std::chrono::steady_clock::now();

    if (shouldSendHeartbeat(now)) {
        if (m_apiClient.sendHeartbeat(m_deviceToken, m_identity, updatedSnapshot, m_networkConfigurationAdapter.describeState())) {
            m_lastHeartbeatAt = now;
            m_hasHeartbeat = true;
            m_status = "heartbeat ok; internet control disabled; " + m_networkConfigurationAdapter.describeState();
        } else {
            m_status = "heartbeat failed";
        }
    }

    if (shouldSendActivity(now)) {
        const bool focusedSent = m_apiClient.sendActivity(m_deviceToken, updatedSnapshot);
        if (focusedSent) {
            m_lastActivityAt = now;
            m_hasActivity = true;
            m_status = "activity ok; internet control disabled; " + m_networkConfigurationAdapter.describeState();
        } else {
            m_status = "activity failed";
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

}  // namespace companion::core
