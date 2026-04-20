#include "companion/service/CompanionConfigStore.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
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

std::string pathString(const std::filesystem::path& path) {
    return path.string();
}

std::filesystem::path userConfigBaseDirectory() {
#ifdef _WIN32
    if (const auto* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0') {
        return std::filesystem::path(appData) / "AIRCompanion";
    }

    return std::filesystem::path(".") / "AIRCompanion";
#else
    if (const auto* xdgConfigHome = std::getenv("XDG_CONFIG_HOME"); xdgConfigHome != nullptr && *xdgConfigHome != '\0') {
        return std::filesystem::path(xdgConfigHome) / "AIRCompanion";
    }
    if (const auto* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "AIRCompanion";
    }

    return std::filesystem::path(".") / "AIRCompanion";
#endif
}

std::filesystem::path machineConfigBaseDirectory() {
#ifdef _WIN32
    if (const auto* programData = std::getenv("PROGRAMDATA"); programData != nullptr && *programData != '\0') {
        return std::filesystem::path(programData) / "AIRCompanion" / "Service";
    }

    return std::filesystem::path(".") / "AIRCompanion" / "Service";
#else
    return std::filesystem::path("/etc") / "air-companion";
#endif
}

std::optional<StoredCompanionConfig> CompanionConfigStore::loadFromPath(const std::string& path) {
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

std::optional<StoredCompanionConfig> CompanionConfigStore::load() const {
    const auto primary = loadFromPath(configPath());
    if (primary.has_value()) {
        return primary;
    }

    const auto backup = loadFromPath(backupConfigPath());
    if (backup.has_value()) {
        (void) save(*backup);
        return backup;
    }

    const auto machine = loadFromPath(pathString(std::filesystem::path(machineConfigDirectory()) / "config.json"));
    if (machine.has_value()) {
        (void) save(*machine);
        return machine;
    }

    return std::nullopt;
}

bool CompanionConfigStore::saveToPath(const std::string& path, const StoredCompanionConfig& config) {
    const auto directory = std::filesystem::path(path).parent_path().string();
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
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

bool CompanionConfigStore::save(const StoredCompanionConfig& config) const {
    const auto primarySaved = saveToPath(configPath(), config);
    const auto backupSaved = saveToPath(backupConfigPath(), config);
    const auto machineSaved = saveToPath(pathString(std::filesystem::path(machineConfigDirectory()) / "config.json"), config);

    return primarySaved || backupSaved || machineSaved;
}

bool CompanionConfigStore::clear() const {
    std::error_code error;
    std::filesystem::remove(configPath(), error);
    std::filesystem::remove(backupConfigPath(), error);
    std::filesystem::remove(pathString(std::filesystem::path(machineConfigDirectory()) / "config.json"), error);
    return !error;
}

std::string CompanionConfigStore::configPath() const {
    return pathString(std::filesystem::path(configDirectory()) / "config.json");
}

std::string CompanionConfigStore::backupConfigPath() const {
    return pathString(std::filesystem::path(configDirectory()) / "config.backup.json");
}

std::string CompanionConfigStore::configDirectory() {
    return pathString(userConfigBaseDirectory());
}

std::string CompanionConfigStore::machineConfigDirectory() {
    return pathString(machineConfigBaseDirectory());
}

}  // namespace companion::service
