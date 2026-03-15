#pragma once

#include <optional>
#include <string>

#include "companion/models/DevicePolicy.h"
#include "companion/networking/CompanionApiClient.h"

namespace companion::core {

class PolicySync {
public:
    PolicySync(networking::CompanionApiClient apiClient, std::string deviceToken);

    std::optional<models::DevicePolicy> refresh();
    const std::string& lastPolicyHash() const;

private:
    networking::CompanionApiClient m_apiClient;
    std::string m_deviceToken;
    std::string m_lastPolicyHash;
};

}  // namespace companion::core

