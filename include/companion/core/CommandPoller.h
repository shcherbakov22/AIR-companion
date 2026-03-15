#pragma once

#include <string>
#include <vector>

#include "companion/models/DeviceCommand.h"
#include "companion/networking/CompanionApiClient.h"

namespace companion::core {

class CommandPoller {
public:
    CommandPoller(networking::CompanionApiClient apiClient, std::string deviceToken);

    std::vector<models::DeviceCommand> poll() const;
    bool acknowledge(const std::string& commandId) const;
    bool submitResult(const std::string& commandId, bool success, const std::string& output) const;

private:
    networking::CompanionApiClient m_apiClient;
    std::string m_deviceToken;
};

}  // namespace companion::core

