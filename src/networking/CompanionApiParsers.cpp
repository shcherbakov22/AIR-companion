#include "companion/networking/CompanionApiParsers.h"

#include <optional>
#include <string>

namespace companion::networking {

namespace {

std::optional<std::string> jsonObjectString(const std::string& body, const std::string& key) {
    const auto keyPos = body.find("\"" + key + "\"");
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    const auto objectStart = body.find('{', keyPos);
    if (objectStart == std::string::npos) {
        return std::nullopt;
    }

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t index = objectStart; index < body.size(); ++index) {
        const char ch = body[index];

        if (inString) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
            continue;
        }

        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                return body.substr(objectStart, index - objectStart + 1);
            }
        }
    }

    return std::nullopt;
}

std::optional<std::string> jsonStringValue(const std::string& body, const std::string& key) {
    const auto keyPos = body.find("\"" + key + "\"");
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    const auto colonPos = body.find(':', keyPos);
    if (colonPos == std::string::npos) {
        return std::nullopt;
    }

    const auto valueStart = body.find_first_not_of(" \t\r\n", colonPos + 1);
    if (valueStart == std::string::npos || body[valueStart] != '"') {
        return std::nullopt;
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

    return std::nullopt;
}

std::optional<bool> jsonBoolValue(const std::string& body, const std::string& key) {
    const auto keyPos = body.find("\"" + key + "\"");
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    const auto colonPos = body.find(':', keyPos);
    if (colonPos == std::string::npos) {
        return std::nullopt;
    }

    const auto truePos = body.find("true", colonPos + 1);
    const auto falsePos = body.find("false", colonPos + 1);
    const auto endPos = body.find_first_of(",}", colonPos + 1);

    if (truePos != std::string::npos && truePos < endPos) {
        return true;
    }
    if (falsePos != std::string::npos && falsePos < endPos) {
        return false;
    }

    return std::nullopt;
}

std::optional<int> jsonIntValue(const std::string& body, const std::string& key) {
    const auto keyPos = body.find("\"" + key + "\"");
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    const auto colonPos = body.find(':', keyPos);
    if (colonPos == std::string::npos) {
        return std::nullopt;
    }

    const auto numberStart = body.find_first_of("-0123456789", colonPos + 1);
    const auto numberEnd = body.find_first_not_of("0123456789", numberStart);
    if (numberStart == std::string::npos) {
        return std::nullopt;
    }

    return std::stoi(body.substr(numberStart, numberEnd - numberStart));
}

std::vector<std::string> jsonStringArray(const std::string& body, const std::string& key) {
    const auto keyPos = body.find("\"" + key + "\"");
    if (keyPos == std::string::npos) {
        return {};
    }

    const auto arrayStart = body.find('[', keyPos);
    if (arrayStart == std::string::npos) {
        return {};
    }

    std::vector<std::string> values;
    bool inString = false;
    bool escaped = false;
    std::string current;

    for (std::size_t index = arrayStart + 1; index < body.size(); ++index) {
        const char ch = body[index];
        if (!inString && ch == ']') {
            break;
        }

        if (inString) {
            if (escaped) {
                current += ch;
                escaped = false;
                continue;
            }

            if (ch == '\\') {
                escaped = true;
                continue;
            }

            if (ch == '"') {
                values.push_back(current);
                current.clear();
                inString = false;
                continue;
            }

            current += ch;
            continue;
        }

        if (ch == '"') {
            inString = true;
        }
    }

    return values;
}

models::DeviceCommandType parseCommandType(const std::string& type) {
    if (type == "refresh_policy") {
        return models::DeviceCommandType::RefreshPolicy;
    }
    if (type == "request_screenshot") {
        return models::DeviceCommandType::RequestScreenshot;
    }
    if (type == "request_camera_capture") {
        return models::DeviceCommandType::RequestCameraCapture;
    }
    if (type == "lock_internet") {
        return models::DeviceCommandType::LockInternet;
    }
    if (type == "unlock_internet") {
        return models::DeviceCommandType::UnlockInternet;
    }
    if (type == "verify_remote_control") {
        return models::DeviceCommandType::VerifyRemoteControl;
    }
    if (type == "start_remote_control") {
        return models::DeviceCommandType::StartRemoteControl;
    }
    if (type == "stop_remote_control") {
        return models::DeviceCommandType::StopRemoteControl;
    }
    return models::DeviceCommandType::Unknown;
}

}  // namespace

std::optional<models::DeviceEnrollment> parseEnrollmentResponse(
    const std::string& responseBody,
    const models::DeviceIdentity& identity,
    const std::string& fallbackUsername
) {
    const auto token = jsonStringValue(responseBody, "token");
    if (!token.has_value()) {
        return std::nullopt;
    }

    models::DeviceEnrollment enrollment{identity, *token};
    if (const auto student = jsonObjectString(responseBody, "student"); student.has_value()) {
        enrollment.identity.studentUsername = jsonStringValue(*student, "username").value_or(fallbackUsername);
    }
    if (const auto device = jsonObjectString(responseBody, "device"); device.has_value()) {
        enrollment.identity.deviceLabel = jsonStringValue(*device, "label").value_or(identity.deviceLabel);
        enrollment.identity.hostname = jsonStringValue(*device, "hostname").value_or(identity.hostname);
    }
    if (const auto web = jsonObjectString(responseBody, "web"); web.has_value()) {
        enrollment.browserLoginUrl = jsonStringValue(*web, "browser_login_url").value_or({});
    }

    return enrollment;
}

std::optional<std::string> parseRenewTokenResponse(const std::string& responseBody) {
    return jsonStringValue(responseBody, "token");
}

std::optional<models::DevicePolicy> parsePolicyResponse(const std::string& responseBody) {
    models::DevicePolicy policy;
    policy.policyHash = jsonStringValue(responseBody, "policy_hash").value_or({});

    if (const auto policyBody = jsonObjectString(responseBody, "policy"); policyBody.has_value()) {
        if (const auto student = jsonObjectString(*policyBody, "student"); student.has_value()) {
            policy.studentDisplayName = jsonStringValue(*student, "display_name").value_or({});
        }

        if (const auto schedule = jsonObjectString(*policyBody, "schedule"); schedule.has_value()) {
            policy.activeScheduleName = jsonStringValue(*schedule, "name").value_or({});
        }

        if (const auto task = jsonObjectString(*policyBody, "task"); task.has_value()) {
            policy.activeTaskName = jsonStringValue(*task, "title").value_or({});
        }

        if (const auto gate = jsonObjectString(*policyBody, "communication_gate"); gate.has_value()) {
            policy.hasUnreadMentorChat = jsonBoolValue(*gate, "has_unread_chat").value_or(false);
            policy.hasUnreadAnnouncements = jsonBoolValue(*gate, "has_unread_announcements").value_or(false);
        }

        if (const auto violations = jsonObjectString(*policyBody, "violations"); violations.has_value()) {
            policy.hasOpenViolations = jsonIntValue(*violations, "open_count").value_or(0) > 0;
        }

        if (const auto capture = jsonObjectString(*policyBody, "capture"); capture.has_value()) {
            policy.shouldCaptureScreen = jsonBoolValue(*capture, "screen_enabled").value_or(true);
            policy.shouldCaptureCamera = jsonBoolValue(*capture, "camera_enabled").value_or(false);
            policy.screenCaptureIntervalSeconds = jsonIntValue(*capture, "screen_interval_seconds").value_or(30);
            policy.cameraCaptureIntervalSeconds = jsonIntValue(*capture, "camera_interval_seconds").value_or(60);
        }

        if (const auto internet = jsonObjectString(*policyBody, "internet_policy"); internet.has_value()) {
            const auto mode = jsonStringValue(*internet, "mode").value_or("block_all");
            if (mode == "allow_all") {
                policy.internetAccessMode = models::InternetAccessMode::AllowAll;
            } else if (mode == "allow_list_only") {
                policy.internetAccessMode = models::InternetAccessMode::AllowListOnly;
            } else {
                policy.internetAccessMode = models::InternetAccessMode::BlockAll;
            }
        }

        if (const auto appControl = jsonObjectString(*policyBody, "app_control"); appControl.has_value()) {
            policy.blockedApps = jsonStringArray(*appControl, "blocked_processes");
        }
    }

    return policy;
}

std::vector<models::DeviceCommand> parseCommandResponse(const std::string& responseBody) {
    const auto commandBody = jsonObjectString(responseBody, "command");
    if (!commandBody.has_value()) {
        return {};
    }

    models::DeviceCommand command;
    command.id = jsonStringValue(*commandBody, "id")
        .value_or(jsonIntValue(*commandBody, "id").has_value()
            ? std::to_string(*jsonIntValue(*commandBody, "id"))
            : std::string{});
    command.status = jsonStringValue(*commandBody, "status").value_or({});
    command.type = parseCommandType(jsonStringValue(*commandBody, "command_type").value_or({}));
    command.payloadJson = jsonObjectString(*commandBody, "payload").value_or("{}");

    if (command.id.empty()) {
        return {};
    }

    return {command};
}

}  // namespace companion::networking
