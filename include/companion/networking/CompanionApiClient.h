#pragma once

#include <optional>
#include <string>
#include <vector>

#include "companion/models/ActivitySnapshot.h"
#include "companion/models/DeviceCommand.h"
#include "companion/models/DeviceIdentity.h"
#include "companion/models/DevicePolicy.h"
#include "companion/networking/HttpClient.h"

namespace companion::networking {

class CompanionApiClient {
public:
    CompanionApiClient(std::string baseUrl, HttpClient httpClient = {});

    std::optional<models::DeviceEnrollment> enroll(
        const std::string& username,
        const std::string& password,
        const models::DeviceIdentity& identity) const;

    std::optional<models::DevicePolicy> fetchPolicy(const std::string& deviceToken) const;

    std::vector<models::DeviceCommand> fetchCommands(const std::string& deviceToken) const;
    bool acknowledgeCommand(const std::string& deviceToken, const std::string& commandId) const;
    bool submitCommandResult(const std::string& deviceToken,
                             const std::string& commandId,
                             bool success,
                             const std::string& output) const;

    bool sendHeartbeat(
        const std::string& deviceToken,
        const models::DeviceIdentity& identity,
        const models::ActivitySnapshot& snapshot,
        const std::string& networkState) const;
    bool sendActivity(const std::string& deviceToken, const models::ActivitySnapshot& snapshot) const;
    const std::string& baseUrl() const;

private:
    std::string m_baseUrl;
    HttpClient m_httpClient;
};

}  // namespace companion::networking
