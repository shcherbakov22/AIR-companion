#include "companion/service/CompanionConfigStore.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace companion::service {

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

std::optional<std::string> extractJsonString(const std::string& body, const std::string& key) {
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

}  // namespace

std::optional<StoredCompanionConfig> CompanionConfigStore::load() const {
    const auto path = configPath();
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const auto body = buffer.str();

    StoredCompanionConfig config;
    config.baseUrl = extractJsonString(body, "base_url").value_or({});
    config.deviceToken = extractJsonString(body, "device_token").value_or({});
    config.rootCaUrl = extractJsonString(body, "root_ca_url").value_or({});
    config.identity.deviceId = extractJsonString(body, "device_id").value_or({});
    config.identity.hostname = extractJsonString(body, "hostname").value_or({});
    config.identity.deviceLabel = extractJsonString(body, "device_label").value_or({});
    config.identity.platform = extractJsonString(body, "platform").value_or({});
    config.identity.appVersion = extractJsonString(body, "app_version").value_or({});
    config.identity.studentUsername = extractJsonString(body, "student_username").value_or({});

    if (config.baseUrl.empty() || config.deviceToken.empty() || config.identity.deviceId.empty()) {
        return std::nullopt;
    }

    return config;
}

bool CompanionConfigStore::save(const StoredCompanionConfig& config) const {
    const auto directory = configDirectory();
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }

    std::ofstream output(configPath(), std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output
        << "{\n"
        << "  \"base_url\": \"" << escapeJson(config.baseUrl) << "\",\n"
        << "  \"device_token\": \"" << escapeJson(config.deviceToken) << "\",\n"
        << "  \"root_ca_url\": \"" << escapeJson(config.rootCaUrl) << "\",\n"
        << "  \"device_id\": \"" << escapeJson(config.identity.deviceId) << "\",\n"
        << "  \"hostname\": \"" << escapeJson(config.identity.hostname) << "\",\n"
        << "  \"device_label\": \"" << escapeJson(config.identity.deviceLabel) << "\",\n"
        << "  \"platform\": \"" << escapeJson(config.identity.platform) << "\",\n"
        << "  \"app_version\": \"" << escapeJson(config.identity.appVersion) << "\",\n"
        << "  \"student_username\": \"" << escapeJson(config.identity.studentUsername) << "\"\n"
        << "}\n";

    return output.good();
}

bool CompanionConfigStore::clear() const {
    std::error_code error;
    std::filesystem::remove(configPath(), error);
    return !error;
}

std::string CompanionConfigStore::configPath() const {
    return configDirectory() + "\\config.json";
}

std::string CompanionConfigStore::configDirectory() {
    if (const auto* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0') {
        return std::string(appData) + "\\AIRCompanion";
    }

    return ".\\AIRCompanion";
}

}  // namespace companion::service
