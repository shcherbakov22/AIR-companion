#include "companion/networking/CompanionApiClient.h"

#include <sstream>
#include <utility>

namespace companion::networking {

namespace {

std::string escapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }

    return escaped;
}

std::string jsonString(const std::string& value) {
    return "\"" + escapeJson(value) + "\"";
}

std::string jsonArray(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        out << jsonString(values[index]);
    }
    out << "]";
    return out.str();
}

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
    const auto openingQuote = body.find('"', colonPos + 1);
    if (colonPos == std::string::npos || openingQuote == std::string::npos) {
        return std::nullopt;
    }

    std::string value;
    for (std::size_t index = openingQuote + 1; index < body.size(); ++index) {
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
    return models::DeviceCommandType::Unknown;
}

std::map<std::string, std::string> jsonHeaders(const std::string& deviceToken = {}) {
    std::map<std::string, std::string> headers{
        {"Accept", "application/json"},
        {"Content-Type", "application/json"},
    };
    if (!deviceToken.empty()) {
        headers.emplace("Authorization", "Bearer " + deviceToken);
    }
    return headers;
}

}  // namespace

CompanionApiClient::CompanionApiClient(std::string baseUrl, HttpClient httpClient)
    : m_baseUrl(std::move(baseUrl)), m_httpClient(std::move(httpClient)) {}

std::optional<models::DeviceEnrollment> CompanionApiClient::enroll(
    const std::string& username,
    const std::string& password,
    const models::DeviceIdentity& identity) const {
    std::ostringstream body;
    body << "{"
         << "\"username\":" << jsonString(username) << ","
         << "\"password\":" << jsonString(password) << ","
         << "\"device_key\":" << jsonString(identity.deviceId) << ","
         << "\"hostname\":" << jsonString(identity.hostname) << ","
         << "\"label\":" << jsonString(identity.deviceLabel) << ","
         << "\"platform\":" << jsonString(identity.platform) << ","
         << "\"app_version\":" << jsonString(identity.appVersion)
         << "}";

    const auto response = m_httpClient.post(m_baseUrl + "/api/companion/enroll", jsonHeaders(), body.str());
    if (response.statusCode < 200 || response.statusCode >= 300) {
        return std::nullopt;
    }

    const auto token = jsonStringValue(response.body, "token");
    if (!token.has_value()) {
        return std::nullopt;
    }

    models::DeviceEnrollment enrollment{identity, *token};
    if (const auto student = jsonObjectString(response.body, "student"); student.has_value()) {
        enrollment.identity.studentUsername = jsonStringValue(*student, "username").value_or(username);
    }
    if (const auto device = jsonObjectString(response.body, "device"); device.has_value()) {
        enrollment.identity.deviceLabel = jsonStringValue(*device, "label").value_or(identity.deviceLabel);
        enrollment.identity.hostname = jsonStringValue(*device, "hostname").value_or(identity.hostname);
    }

    return enrollment;
}

std::optional<std::string> CompanionApiClient::renewToken(const std::string& deviceToken) const {
    const auto response = m_httpClient.post(
        m_baseUrl + "/api/companion/token/renew",
        jsonHeaders(deviceToken),
        "{}"
    );
    if (response.statusCode < 200 || response.statusCode >= 300) {
        return std::nullopt;
    }

    return jsonStringValue(response.body, "token");
}

std::optional<models::DevicePolicy> CompanionApiClient::fetchPolicy(const std::string& deviceToken) const {
    const auto response = m_httpClient.get(m_baseUrl + "/api/companion/policy", jsonHeaders(deviceToken));
    if (response.statusCode < 200 || response.statusCode >= 300) {
        return std::nullopt;
    }

    models::DevicePolicy policy;
    policy.policyHash = jsonStringValue(response.body, "policy_hash").value_or({});

    if (const auto policyBody = jsonObjectString(response.body, "policy"); policyBody.has_value()) {
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
    }

    return policy;
}

std::vector<models::DeviceCommand> CompanionApiClient::fetchCommands(const std::string& deviceToken) const {
    const auto response = m_httpClient.get(m_baseUrl + "/api/companion/commands/next", jsonHeaders(deviceToken));
    if (response.statusCode < 200 || response.statusCode >= 300) {
        return {};
    }

    const auto commandBody = jsonObjectString(response.body, "command");
    if (!commandBody.has_value()) {
        return {};
    }

    models::DeviceCommand command;
    command.id = jsonStringValue(*commandBody, "id").value_or({});
    command.status = jsonStringValue(*commandBody, "status").value_or({});
    command.type = parseCommandType(jsonStringValue(*commandBody, "command_type").value_or({}));
    command.payloadJson = jsonObjectString(*commandBody, "payload").value_or("{}");

    if (command.id.empty()) {
        return {};
    }

    return {command};
}

bool CompanionApiClient::acknowledgeCommand(const std::string& deviceToken, const std::string& commandId) const {
    const auto response = m_httpClient.post(
        m_baseUrl + "/api/companion/commands/" + commandId + "/acknowledge",
        jsonHeaders(deviceToken),
        "{}"
    );
    return response.statusCode >= 200 && response.statusCode < 300;
}

bool CompanionApiClient::submitCommandResult(const std::string& deviceToken,
                                             const std::string& commandId,
                                             bool success,
                                             const std::string& output) const {
    std::ostringstream body;
    body << "{"
         << "\"status\":\"" << (success ? "completed" : "failed") << "\","
         << "\"payload\":{\"output\":" << jsonString(output) << "}}";

    const auto response = m_httpClient.post(
        m_baseUrl + "/api/companion/commands/" + commandId + "/result",
        jsonHeaders(deviceToken),
        body.str()
    );
    return response.statusCode >= 200 && response.statusCode < 300;
}

bool CompanionApiClient::sendHeartbeat(const std::string& deviceToken,
                                       const models::DeviceIdentity& identity,
                                       const models::ActivitySnapshot& snapshot,
                                       const std::string& networkState) const {
    std::ostringstream body;
    body << "{"
         << "\"label\":" << jsonString(identity.deviceLabel) << ","
         << "\"hostname\":" << jsonString(identity.hostname) << ","
         << "\"app_version\":" << jsonString(identity.appVersion) << ","
         << "\"ipv4\":" << jsonString(snapshot.networkIdentity.ipv4) << ","
         << "\"mac_address\":" << jsonString(snapshot.networkIdentity.macAddress) << ","
         << "\"gateway_ipv4\":" << jsonString(snapshot.networkIdentity.gatewayIpv4) << ","
         << "\"network_adapter_name\":" << jsonString(snapshot.networkIdentity.adapterName) << ","
         << "\"meta\":{"
         << "\"focused_app\":" << jsonString(snapshot.focusedApp) << ","
         << "\"focused_window_title\":" << jsonString(snapshot.focusedWindowTitle) << ","
         << "\"active_browser_domain\":" << jsonString(snapshot.activeBrowserDomain) << ","
         << "\"open_apps\":" << jsonArray(snapshot.openApps) << ","
         << "\"dns_ipv4\":" << jsonString(snapshot.networkIdentity.dnsIpv4) << ","
         << "\"air_gateway_state\":" << jsonString(networkState)
         << "}}";

    const auto response = m_httpClient.post(
        m_baseUrl + "/api/companion/heartbeat",
        jsonHeaders(deviceToken),
        body.str()
    );

    return response.statusCode >= 200 && response.statusCode < 300;
}

bool CompanionApiClient::sendActivity(const std::string& deviceToken, const models::ActivitySnapshot& snapshot) const {
    std::ostringstream focusedBody;
    focusedBody << "{"
                << "\"event_type\":\"focused_app\","
                << "\"app_name\":" << jsonString(snapshot.focusedApp) << ","
                << "\"window_title\":" << jsonString(snapshot.focusedWindowTitle) << ","
                << "\"browser_domain\":" << jsonString(snapshot.activeBrowserDomain) << ","
                << "\"payload\":{"
                << "\"network_adapter_name\":" << jsonString(snapshot.networkIdentity.adapterName) << ","
                << "\"ipv4\":" << jsonString(snapshot.networkIdentity.ipv4)
                << "}}";

    const auto focusedResponse = m_httpClient.post(
        m_baseUrl + "/api/companion/activity",
        jsonHeaders(deviceToken),
        focusedBody.str()
    );

    std::ostringstream openAppsBody;
    openAppsBody << "{"
                 << "\"event_type\":\"open_apps\","
                 << "\"app_name\":" << jsonString(snapshot.focusedApp) << ","
                 << "\"window_title\":" << jsonString(snapshot.focusedWindowTitle) << ","
                 << "\"browser_domain\":" << jsonString(snapshot.activeBrowserDomain) << ","
                 << "\"payload\":{"
                 << "\"apps\":" << jsonArray(snapshot.openApps)
                 << "}}";

    const auto openAppsResponse = m_httpClient.post(
        m_baseUrl + "/api/companion/activity",
        jsonHeaders(deviceToken),
        openAppsBody.str()
    );

    return focusedResponse.statusCode >= 200 && focusedResponse.statusCode < 300
        && openAppsResponse.statusCode >= 200 && openAppsResponse.statusCode < 300;
}

bool CompanionApiClient::uploadScreenCapture(const std::string& deviceToken,
                                             const std::string& filePath,
                                             const models::ActivitySnapshot& snapshot,
                                             const std::string& contentType) const {
    const std::map<std::string, std::string> fields{
        {"app_name", snapshot.focusedApp},
        {"window_title", snapshot.focusedWindowTitle},
        {"browser_domain", snapshot.activeBrowserDomain},
    };

    const auto response = m_httpClient.postMultipart(
        m_baseUrl + "/api/companion/captures/screen",
        jsonHeaders(deviceToken),
        fields,
        "capture",
        filePath,
        contentType
    );
    return response.statusCode >= 200 && response.statusCode < 300;
}

bool CompanionApiClient::uploadCameraCapture(const std::string& deviceToken,
                                             const std::string& filePath,
                                             const models::ActivitySnapshot& snapshot,
                                             const std::string& contentType) const {
    const std::map<std::string, std::string> fields{
        {"app_name", snapshot.focusedApp},
        {"window_title", snapshot.focusedWindowTitle},
        {"browser_domain", snapshot.activeBrowserDomain},
    };

    const auto response = m_httpClient.postMultipart(
        m_baseUrl + "/api/companion/captures/camera",
        jsonHeaders(deviceToken),
        fields,
        "capture",
        filePath,
        contentType
    );
    return response.statusCode >= 200 && response.statusCode < 300;
}

const std::string& CompanionApiClient::baseUrl() const {
    return m_baseUrl;
}

}  // namespace companion::networking
