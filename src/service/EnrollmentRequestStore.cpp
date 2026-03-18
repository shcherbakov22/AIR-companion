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

    if (request->baseUrl.empty() || request->username.empty() || request->password.empty()) {
        return std::nullopt;
    }

    return request;
}

bool EnrollmentRequestStore::save(const EnrollmentRequest& request) const {
    const auto directory = requestDirectory();
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }

    hidePath(directory);

    std::ofstream output(requestPath(), std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output
        << "{\n"
        << "  \"base_url\": \"" << escapeJson(request.baseUrl) << "\",\n"
        << "  \"username\": \"" << escapeJson(request.username) << "\",\n"
        << "  \"password\": \"" << escapeJson(request.password) << "\",\n"
        << "  \"device_label\": \"" << escapeJson(request.deviceLabel) << "\",\n"
        << "  \"root_ca_url\": \"" << escapeJson(request.rootCaUrl) << "\"\n"
        << "}\n";

    output.flush();
    hidePath(requestPath());
    return output.good();
}

bool EnrollmentRequestStore::saveTemplate() const {
    return save(EnrollmentRequest{
        .baseUrl = "https://192.168.11.228",
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
