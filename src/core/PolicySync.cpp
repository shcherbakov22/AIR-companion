#include "companion/core/PolicySync.h"

#include <utility>

namespace companion::core {

PolicySync::PolicySync(networking::CompanionApiClient apiClient, std::string deviceToken)
    : m_apiClient(std::move(apiClient)), m_deviceToken(std::move(deviceToken)) {}

std::optional<models::DevicePolicy> PolicySync::refresh() {
    auto policy = m_apiClient.fetchPolicy(m_deviceToken);
    if (policy.has_value()) {
        m_lastPolicyHash = policy->policyHash;
    }

    return policy;
}

const std::string& PolicySync::lastPolicyHash() const {
    return m_lastPolicyHash;
}

}  // namespace companion::core

