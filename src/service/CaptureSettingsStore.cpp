#include "companion/service/CaptureSettingsStore.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
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

std::optional<bool> extractJsonBool(const std::string& body, const std::string& key) {
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

std::optional<int> extractJsonInt(const std::string& body, const std::string& key) {
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

void hidePathOnWindows(const std::string& path) {
#ifdef _WIN32
    SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
#else
    (void)path;
#endif
}

std::string defaultCaptureBaseDirectory() {
    if (const auto* programData = std::getenv("PROGRAMDATA"); programData != nullptr && *programData != '\0') {
        return std::string(programData) + "\\AIRCompanion\\Captures";
    }

    return ".\\AIRCompanion\\Captures";
}

std::string normalizeOutputDirectory(const std::string& configuredPath) {
    if (configuredPath.empty()) {
        return defaultCaptureBaseDirectory();
    }

    std::filesystem::path path(configuredPath);
    if (path.is_absolute()) {
        return path.string();
    }

    return (std::filesystem::path(defaultCaptureBaseDirectory()) / path).string();
}

}  // namespace

InternalCaptureSettings CaptureSettingsStore::loadOrCreate() const {
    std::ifstream input(settingsPath(), std::ios::binary);
    if (!input.is_open()) {
        InternalCaptureSettings defaults;
        defaults.screenOutputDirectory = normalizeOutputDirectory(defaults.screenOutputDirectory);
        defaults.cameraOutputDirectory = normalizeOutputDirectory(defaults.cameraOutputDirectory);
        (void)save(defaults);
        return defaults;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const auto body = buffer.str();

    InternalCaptureSettings settings;
    settings.allowScreenCapture = extractJsonBool(body, "allow_screen_capture").value_or(settings.allowScreenCapture);
    settings.allowCameraCapture = extractJsonBool(body, "allow_camera_capture").value_or(settings.allowCameraCapture);
    settings.minimumScreenIntervalSeconds = extractJsonInt(body, "minimum_screen_interval_seconds").value_or(settings.minimumScreenIntervalSeconds);
    settings.minimumCameraIntervalSeconds = extractJsonInt(body, "minimum_camera_interval_seconds").value_or(settings.minimumCameraIntervalSeconds);
    settings.screenOutputDirectory = normalizeOutputDirectory(extractJsonString(body, "screen_output_directory").value_or(settings.screenOutputDirectory));
    settings.cameraOutputDirectory = normalizeOutputDirectory(extractJsonString(body, "camera_output_directory").value_or(settings.cameraOutputDirectory));
    settings.screenContentType = extractJsonString(body, "screen_content_type").value_or(settings.screenContentType);
    settings.cameraContentType = extractJsonString(body, "camera_content_type").value_or(settings.cameraContentType);
    return settings;
}

bool CaptureSettingsStore::save(const InternalCaptureSettings& settings) const {
    const auto directory = settingsDirectory();
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }

    hidePathOnWindows(directory);

    std::ofstream output(settingsPath(), std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output
        << "{\n"
        << "  \"allow_screen_capture\": " << (settings.allowScreenCapture ? "true" : "false") << ",\n"
        << "  \"allow_camera_capture\": " << (settings.allowCameraCapture ? "true" : "false") << ",\n"
        << "  \"minimum_screen_interval_seconds\": " << settings.minimumScreenIntervalSeconds << ",\n"
        << "  \"minimum_camera_interval_seconds\": " << settings.minimumCameraIntervalSeconds << ",\n"
        << "  \"screen_output_directory\": \"" << escapeJson(normalizeOutputDirectory(settings.screenOutputDirectory)) << "\",\n"
        << "  \"camera_output_directory\": \"" << escapeJson(normalizeOutputDirectory(settings.cameraOutputDirectory)) << "\",\n"
        << "  \"screen_content_type\": \"" << escapeJson(settings.screenContentType) << "\",\n"
        << "  \"camera_content_type\": \"" << escapeJson(settings.cameraContentType) << "\"\n"
        << "}\n";

    output.close();
    if (!output.good()) {
        return false;
    }

    hidePathOnWindows(settingsPath());
    return true;
}

std::string CaptureSettingsStore::settingsPath() const {
    return settingsDirectory() + "\\capture-settings.json";
}

std::string CaptureSettingsStore::settingsDirectory() {
    if (const auto* programData = std::getenv("PROGRAMDATA"); programData != nullptr && *programData != '\0') {
        return std::string(programData) + "\\AIRCompanion\\Internal";
    }

    return ".\\AIRCompanion\\Internal";
}

}  // namespace companion::service
