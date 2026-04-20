#include "companion/core/EnforcementCoordinator.h"

namespace companion::core {

namespace {

std::string jsonStringValue(const std::string& body, const std::string& key) {
    const auto keyPos = body.find("\"" + key + "\"");
    if (keyPos == std::string::npos) {
        return {};
    }

    const auto colonPos = body.find(':', keyPos);
    if (colonPos == std::string::npos) {
        return {};
    }

    const auto valueStart = body.find_first_not_of(" \t\r\n", colonPos + 1);
    if (valueStart == std::string::npos || body[valueStart] != '"') {
        return {};
    }

    std::string value;
    for (std::size_t index = valueStart + 1; index < body.size(); ++index) {
        const char ch = body[index];
        if (ch == '\\' && index + 1 < body.size()) {
            value += body[index + 1];
            ++index;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value += ch;
    }

    return {};
}

int jsonIntValue(const std::string& body, const std::string& key, int fallback) {
    const auto keyPos = body.find("\"" + key + "\"");
    if (keyPos == std::string::npos) {
        return fallback;
    }

    const auto colonPos = body.find(':', keyPos);
    if (colonPos == std::string::npos) {
        return fallback;
    }

    const auto numberStart = body.find_first_of("-0123456789", colonPos + 1);
    if (numberStart == std::string::npos) {
        return fallback;
    }

    const auto numberEnd = body.find_first_not_of("0123456789", numberStart);
    return std::stoi(body.substr(numberStart, numberEnd - numberStart));
}

}  // namespace

EnforcementCoordinator::EnforcementCoordinator(adapters::IEnforcementAdapter& enforcementAdapter)
    : m_enforcementAdapter(enforcementAdapter) {}

void EnforcementCoordinator::applyPolicy(const models::DevicePolicy& policy, const models::ActivitySnapshot& snapshot) {
    m_enforcementAdapter.applyPolicy(policy, snapshot);
    m_enforcementAdapter.terminateBlockedApps(policy.blockedApps);
}

EnforcementCoordinator::CommandExecutionResult EnforcementCoordinator::applyCommand(const models::DeviceCommand& command) {
    switch (command.type) {
        case models::DeviceCommandType::ShowMessage: {
            auto title = jsonStringValue(command.payloadJson, "title");
            auto body = jsonStringValue(command.payloadJson, "body");
            const auto displaySeconds = jsonIntValue(command.payloadJson, "display_seconds", 15);

            if (title.empty()) {
                title = "Message from mentor";
            }

            if (body.empty()) {
                return {false, "message body missing"};
            }

            std::string error;
            const bool success = m_enforcementAdapter.showMessage(title, body, displaySeconds, error);
            return {success, success ? "message displayed" : error};
        }
        case models::DeviceCommandType::LockInternet:
        case models::DeviceCommandType::UnlockInternet:
        case models::DeviceCommandType::RefreshPolicy:
        case models::DeviceCommandType::RequestScreenshot:
        case models::DeviceCommandType::RequestCameraCapture:
        case models::DeviceCommandType::VerifyRemoteControl:
        case models::DeviceCommandType::StartRemoteControl:
        case models::DeviceCommandType::StopRemoteControl:
        case models::DeviceCommandType::Unknown:
        default:
            break;
    }

    return {};
}

}  // namespace companion::core

