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
         << "\"device_id\":" << jsonString(identity.deviceId) << ","
         << "\"hostname\":" << jsonString(identity.hostname) << ","
         << "\"label\":" << jsonString(identity.deviceLabel) << ","
         << "\"platform\":" << jsonString(identity.platform) << ","
         << "\"app_version\":" << jsonString(identity.appVersion)
         << "}";

    (void)m_httpClient.post(m_baseUrl + "/api/companion/enroll", jsonHeaders(), body.str());

    models::DeviceEnrollment enrollment{identity, "stub-device-token-for-" + username};
    return enrollment;
}

std::optional<models::DevicePolicy> CompanionApiClient::fetchPolicy(const std::string& deviceToken) const {
    (void)m_httpClient.get(m_baseUrl + "/api/companion/policy", jsonHeaders(deviceToken));
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

const std::string& CompanionApiClient::baseUrl() const {
    return m_baseUrl;
}

}  // namespace companion::networking
