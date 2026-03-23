#include "companion/service/EnrollmentRequestStore.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

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

void hidePath(const std::string& path) {
#ifdef _WIN32
    const auto wideLength = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wideLength <= 0) {
        return;
    }

    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), wideLength);
    SetFileAttributesW(wide.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
#else
    (void) path;
#endif
}

void normalizeWritablePath(const std::string& path) {
#ifdef _WIN32
    const auto wideLength = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wideLength <= 0) {
        return;
    }

    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), wideLength);
    SetFileAttributesW(wide.c_str(), FILE_ATTRIBUTE_NORMAL);
#else
    (void) path;
#endif
}

}  // namespace

std::optional<EnrollmentRequest> EnrollmentRequestStore::loadDraft() const {
    std::ifstream input(requestPath(), std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const auto body = buffer.str();

    EnrollmentRequest request;
    request.baseUrl = extractJsonString(body, "base_url").value_or({});
    request.enrollmentToken = extractJsonString(body, "enrollment_token").value_or({});
    request.username = extractJsonString(body, "username").value_or({});
    request.password = extractJsonString(body, "password").value_or({});
    request.deviceLabel = extractJsonString(body, "device_label").value_or({});
    request.rootCaUrl = extractJsonString(body, "root_ca_url").value_or({});

    return request;
}

std::optional<EnrollmentRequest> EnrollmentRequestStore::load() const {
    const auto request = loadDraft();
    if (!request.has_value()) {
        return std::nullopt;
    }

    if (request->baseUrl.empty()) {
        return std::nullopt;
    }

    if (request->enrollmentToken.empty() && (request->username.empty() || request->password.empty())) {
        return std::nullopt;
    }

    return request;
}

bool EnrollmentRequestStore::save(const EnrollmentRequest& request) const {
    return !saveWithError(request).has_value();
}

std::optional<std::string> EnrollmentRequestStore::saveWithError(const EnrollmentRequest& request) const {
    const auto directory = requestDirectory();
    const auto path = requestPath();
    const auto tempPath = path + ".tmp";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return "Failed to create enrollment directory '" + directory + "': " + error.message();
    }

    error.clear();
    const auto pathExists = std::filesystem::exists(path, error);
    if (error) {
        return "Failed to inspect enrollment request path '" + path + "': " + error.message();
    }

    error.clear();
    const auto pathIsDirectory = std::filesystem::is_directory(path, error);
    if (error) {
        return "Failed to inspect enrollment request path '" + path + "': " + error.message();
    }

    if (pathExists && pathIsDirectory) {
        error.clear();
        std::filesystem::remove_all(path, error);
        if (error) {
            return "Failed to remove malformed enrollment request directory '" + path + "': " + error.message();
        }
    }

    normalizeWritablePath(path);
    normalizeWritablePath(tempPath);
    hidePath(directory);

    std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return "Failed to open enrollment request file '" + path + "' for writing.";
    }

    output
        << "{\n"
        << "  \"base_url\": \"" << escapeJson(request.baseUrl) << "\",\n"
        << "  \"enrollment_token\": \"" << escapeJson(request.enrollmentToken) << "\",\n"
        << "  \"username\": \"" << escapeJson(request.username) << "\",\n"
        << "  \"password\": \"" << escapeJson(request.password) << "\",\n"
        << "  \"device_label\": \"" << escapeJson(request.deviceLabel) << "\",\n"
        << "  \"root_ca_url\": \"" << escapeJson(request.rootCaUrl) << "\"\n"
        << "}\n";

    output.flush();
    if (!output.good()) {
        return "Failed to flush enrollment request file '" + path + "'.";
    }

    output.close();
    if (!output.good()) {
        return "Failed to close enrollment request file '" + path + "'.";
    }

    std::filesystem::rename(tempPath, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(tempPath, path, error);
        if (error) {
            return "Failed to replace enrollment request file '" + path + "': " + error.message();
        }
    }

    hidePath(path);

    return std::nullopt;
}

bool EnrollmentRequestStore::saveTemplate() const {
    return save(EnrollmentRequest{
        .baseUrl = "https://192.168.11.228",
        .enrollmentToken = {},
        .username = {},
        .password = {},
        .deviceLabel = {},
        .rootCaUrl = {},
    });
}

bool EnrollmentRequestStore::clear() const {
    std::error_code error;
    std::filesystem::remove(requestPath(), error);
    return !error;
}

std::string EnrollmentRequestStore::requestPath() const {
    return requestDirectory() + "\\enrollment-request.json";
}

std::string EnrollmentRequestStore::requestDirectory() {
    if (const auto* programData = std::getenv("PROGRAMDATA"); programData != nullptr && *programData != '\0') {
        return std::string(programData) + "\\AIRCompanion\\Internal";
    }

    return ".\\AIRCompanion\\Internal";
}

}  // namespace companion::service
