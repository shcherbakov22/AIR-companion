#include "companion/networking/CompanionApiClient.h"
#include "companion/networking/CompanionApiParsers.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

std::optional<std::string> extractJsonString(const std::string& body, const std::string& key) {
    const auto keyPos = body.find("\"" + key + "\"");
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    const auto colonPos = body.find(':', keyPos);
    const auto quotePos = body.find('"', colonPos + 1);
    if (colonPos == std::string::npos || quotePos == std::string::npos) {
        return std::nullopt;
    }

    std::string value;
    for (std::size_t index = quotePos + 1; index < body.size(); ++index) {
        const auto ch = body[index];
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

bool extractJsonBool(const std::string& body, const std::string& key, bool fallback = false) {
    const auto keyPos = body.find("\"" + key + "\"");
    if (keyPos == std::string::npos) {
        return fallback;
    }

    const auto colonPos = body.find(':', keyPos);
    if (colonPos == std::string::npos) {
        return fallback;
    }

    const auto valueStart = body.find_first_not_of(" \t\r\n", colonPos + 1);
    if (valueStart == std::string::npos) {
        return fallback;
    }

    if (body.compare(valueStart, 4, "true") == 0) {
        return true;
    }
    if (body.compare(valueStart, 5, "false") == 0) {
        return false;
    }

    return fallback;
}

std::string jsonOpenAppsArray(const std::vector<models::OpenAppEntry>& values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        out << "{"
            << "\"app_name\":" << jsonString(values[index].appName) << ","
            << "\"window_title\":" << jsonString(values[index].windowTitle)
            << "}";
    }
    out << "]";
    return out.str();
}

void appendDebugLog(const std::string& line) {
#ifdef _WIN32
    const char* appData = std::getenv("APPDATA");
    if (appData == nullptr || *appData == '\0') {
        return;
    }

    const auto logDirectory = std::filesystem::path(appData) / "AIRCompanion";
    std::error_code errorCode;
    std::filesystem::create_directories(logDirectory, errorCode);

    std::ofstream output(logDirectory / "debug.log", std::ios::app);
    if (!output.is_open()) {
        return;
    }

    output << line << '\n';
#else
    (void) line;
#endif
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

    return parseEnrollmentResponse(response.body, identity, username);
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

    return parseRenewTokenResponse(response.body);
}

std::optional<models::DevicePolicy> CompanionApiClient::fetchPolicy(const std::string& deviceToken) const {
    const auto response = m_httpClient.get(m_baseUrl + "/api/companion/policy", jsonHeaders(deviceToken));
    if (response.statusCode < 200 || response.statusCode >= 300) {
        return std::nullopt;
    }

    return parsePolicyResponse(response.body);
}

std::vector<models::DeviceCommand> CompanionApiClient::fetchCommands(const std::string& deviceToken) const {
    const auto response = m_httpClient.get(m_baseUrl + "/api/companion/commands/next", jsonHeaders(deviceToken));
    appendDebugLog("fetchCommands status=" + std::to_string(response.statusCode) + " body=" + response.body);
    if (response.statusCode < 200 || response.statusCode >= 300) {
        return {};
    }

    const auto commands = parseCommandResponse(response.body);
    if (commands.empty()) {
        appendDebugLog("fetchCommands parsed empty command id");
        return {};
    }

    const auto& command = commands.front();
    appendDebugLog("fetchCommands parsed command id=" + command.id + " type=" + std::to_string(static_cast<int>(command.type)));

    return commands;
}

bool CompanionApiClient::acknowledgeCommand(const std::string& deviceToken, const std::string& commandId) const {
    const auto response = m_httpClient.post(
        m_baseUrl + "/api/companion/commands/" + commandId + "/acknowledge",
        jsonHeaders(deviceToken),
        "{}"
    );
    appendDebugLog("acknowledgeCommand id=" + commandId + " status=" + std::to_string(response.statusCode) + " body=" + response.body);
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
    appendDebugLog("submitCommandResult id=" + commandId + " success=" + std::string(success ? "true" : "false") + " status=" + std::to_string(response.statusCode) + " body=" + response.body);
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
         << "\"remote_control_ready\":" << (snapshot.remoteAccessState.ready ? "true" : "false") << ","
         << "\"remote_control_active\":" << (snapshot.remoteAccessState.active ? "true" : "false") << ","
         << "\"remote_control_port\":" << snapshot.remoteAccessState.port << ","
         << "\"remote_control_failure_reason\":" << jsonString(snapshot.remoteAccessState.failureReason) << ","
         << "\"meta\":{"
         << "\"focused_app\":" << jsonString(snapshot.focusedApp) << ","
         << "\"focused_window_title\":" << jsonString(snapshot.focusedWindowTitle) << ","
         << "\"active_browser_domain\":" << jsonString(snapshot.activeBrowserDomain) << ","
         << "\"open_apps\":" << jsonOpenAppsArray(snapshot.openApps) << ","
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
                 << "\"apps\":" << jsonOpenAppsArray(snapshot.openApps)
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

std::optional<models::UpdateManifest> CompanionApiClient::fetchUpdateManifest() const {
    const auto response = m_httpClient.get(m_baseUrl + "/api/companion/update-manifest", {
        {"Accept", "application/json"},
    });
    if (response.statusCode < 200 || response.statusCode >= 300) {
        return std::nullopt;
    }

    models::UpdateManifest manifest;
    manifest.available = extractJsonBool(response.body, "available");
    manifest.version = extractJsonString(response.body, "version").value_or({});
    manifest.channel = extractJsonString(response.body, "channel").value_or("stable");
    manifest.mandatory = extractJsonBool(response.body, "mandatory");
    manifest.downloadUrl = extractJsonString(response.body, "download_url").value_or({});
    manifest.sha256 = extractJsonString(response.body, "sha256").value_or({});

    if (!manifest.available || manifest.version.empty() || manifest.downloadUrl.empty() || manifest.sha256.empty()) {
        return std::nullopt;
    }

    return manifest;
}

bool CompanionApiClient::downloadFile(const std::string& url, const std::string& filePath) const {
    return m_httpClient.downloadToFile(url, {
        {"Accept", "*/*"},
    }, filePath);
}

const std::string& CompanionApiClient::baseUrl() const {
    return m_baseUrl;
}

}  // namespace companion::networking
