#include "companion/core/CommandPoller.h"

#include <utility>

namespace companion::core {

CommandPoller::CommandPoller(networking::CompanionApiClient apiClient, std::string deviceToken)
    : m_apiClient(std::move(apiClient)), m_deviceToken(std::move(deviceToken)) {}

std::vector<models::DeviceCommand> CommandPoller::poll() const {
    return m_apiClient.fetchCommands(m_deviceToken);
}

bool CommandPoller::acknowledge(const std::string& commandId) const {
    return m_apiClient.acknowledgeCommand(m_deviceToken, commandId);
}

bool CommandPoller::submitResult(const std::string& commandId, bool success, const std::string& output) const {
    return m_apiClient.submitCommandResult(m_deviceToken, commandId, success, output);
}

}  // namespace companion::core

