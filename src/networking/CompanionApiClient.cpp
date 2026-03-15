#include "companion/networking/CompanionApiClient.h"

#include <utility>

namespace companion::networking {

CompanionApiClient::CompanionApiClient(std::string baseUrl, HttpClient httpClient)
    : m_baseUrl(std::move(baseUrl)), m_httpClient(std::move(httpClient)) {}

std::optional<models::DeviceEnrollment> CompanionApiClient::enroll(
    const std::string& username,
    const std::string&,
    const models::DeviceIdentity& identity) const {
    models::DeviceEnrollment enrollment{identity, "stub-device-token-for-" + username};
    return enrollment;
}

std::optional<models::DevicePolicy> CompanionApiClient::fetchPolicy(const std::string&) const {
    models::DevicePolicy policy;
    policy.policyHash = "stub-policy-hash";
    policy.studentDisplayName = "Stub student";
    policy.activeScheduleName = "Schedule";
    policy.activeTaskName = "Coding";
    policy.internetAccessMode = models::InternetAccessMode::AllowAll;
    return policy;
}

std::vector<models::DeviceCommand> CompanionApiClient::fetchCommands(const std::string&) const {
    return {};
}

bool CompanionApiClient::acknowledgeCommand(const std::string&, const std::string&) const {
    return true;
}

bool CompanionApiClient::submitCommandResult(const std::string&,
                                             const std::string&,
                                             bool,
                                             const std::string&) const {
    return true;
}

bool CompanionApiClient::sendHeartbeat(const std::string&, const models::ActivitySnapshot&) const {
    return true;
}

bool CompanionApiClient::sendActivity(const std::string&, const models::ActivitySnapshot&) const {
    return true;
}

}  // namespace companion::networking

