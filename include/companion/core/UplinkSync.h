#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "companion/adapters/INetworkConfigurationAdapter.h"
#include "companion/models/ActivitySnapshot.h"
#include "companion/models/DeviceIdentity.h"
#include "companion/models/DevicePolicy.h"
#include "companion/networking/CompanionApiClient.h"

namespace companion::core {

class UplinkSync {
public:
    UplinkSync(networking::CompanionApiClient apiClient,
               std::string deviceToken,
               models::DeviceIdentity identity,
               adapters::INetworkConfigurationAdapter& networkConfigurationAdapter);

    void sync(const models::ActivitySnapshot& snapshot, const std::optional<models::DevicePolicy>& policy);
    bool uploadScreenCapture(const std::string& filePath,
                             const models::ActivitySnapshot& snapshot,
                             const std::string& contentType);
    bool uploadCameraCapture(const std::string& filePath,
                             const models::ActivitySnapshot& snapshot,
                             const std::string& contentType);
    std::string statusSummary() const;

private:
    static std::string hostFromUrl(const std::string& url);
    bool shouldSendHeartbeat(std::chrono::steady_clock::time_point now) const;
    bool shouldSendActivity(std::chrono::steady_clock::time_point now) const;

    networking::CompanionApiClient m_apiClient;
    std::string m_deviceToken;
    models::DeviceIdentity m_identity;
    adapters::INetworkConfigurationAdapter& m_networkConfigurationAdapter;
    std::chrono::steady_clock::time_point m_lastHeartbeatAt{};
    std::chrono::steady_clock::time_point m_lastActivityAt{};
    bool m_hasHeartbeat{false};
    bool m_hasActivity{false};
    std::string m_status{"uplink idle"};
};

}  // namespace companion::core
