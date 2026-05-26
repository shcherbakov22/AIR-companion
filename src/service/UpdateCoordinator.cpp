#include "companion/service/UpdateCoordinator.h"
#include "companion/support/LocalLog.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#endif

namespace companion::service {

namespace {

constexpr auto kUpdateCheckInterval = std::chrono::minutes(15);
constexpr auto kUpdateFailureRetryInterval = std::chrono::minutes(1);
constexpr auto kUpdateLaunchGracePeriod = std::chrono::minutes(2);

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

std::vector<int> parseVersion(const std::string& value) {
    std::vector<int> parts;
    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, '.')) {
        if (token.empty()) {
            parts.push_back(0);
            continue;
        }

        int parsed = 0;
        bool sawDigit = false;
        for (const auto ch : token) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                break;
            }

            sawDigit = true;
            parsed = (parsed * 10) + (ch - '0');
        }

        parts.push_back(sawDigit ? parsed : 0);
    }

    return parts;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string tempDirectoryPath() {
#ifdef _WIN32
    const auto* temp = std::getenv("TEMP");
    if (temp != nullptr && *temp != '\0') {
        return temp;
    }
#endif
    return std::filesystem::temp_directory_path().string();
}

}  // namespace

UpdateCoordinator::UpdateCoordinator(networking::CompanionApiClient apiClient, std::string currentVersion)
    : m_apiClient(std::move(apiClient)),
      m_currentVersion(std::move(currentVersion)) {}

void UpdateCoordinator::tick() {
    if (m_updateInProgress) {
        if ((std::chrono::steady_clock::now() - m_updateLaunchedAt) >= kUpdateLaunchGracePeriod) {
            m_updateInProgress = false;
            m_status = "update retry after stalled launch";
            companion::support::appendDebugLog("update check: updater grace period expired; retrying checks current=" + m_currentVersion);
        } else {
            return;
        }
    }

    if (!shouldCheckNow()) {
        return;
    }

    m_lastCheck = std::chrono::steady_clock::now();
    companion::support::appendDebugLog("update check: started current=" + m_currentVersion);
    const auto manifest = m_apiClient.fetchUpdateManifest();
    if (!manifest.has_value() || !manifest->available) {
        m_status = "updates unavailable";
        companion::support::appendDebugLog("update check: manifest unavailable current=" + m_currentVersion);
        scheduleFailureRetry();
        return;
    }

    if (!isNewerVersion(manifest->version, m_currentVersion)) {
        m_status = "updates current";
        companion::support::appendDebugLog("update check: current version " + m_currentVersion + " already satisfies manifest " + manifest->version);
        return;
    }

    companion::support::appendDebugLog(
        "update check: update available manifest=" + manifest->version
        + " current=" + m_currentVersion
        + " size=" + std::to_string(manifest->sizeBytes)
        + " url=" + manifest->downloadUrl
    );
    const auto packagePath = stagePackagePath(*manifest);
    if (!m_apiClient.downloadFile(manifest->downloadUrl, packagePath)) {
        m_status = "update download failed";
        companion::support::appendDebugLog("update download: failed version=" + manifest->version + " path=" + packagePath);
        scheduleFailureRetry();
        return;
    }

    std::error_code fileSizeError;
    const auto downloadedSize = std::filesystem::file_size(packagePath, fileSizeError);
    if (fileSizeError || (manifest->sizeBytes > 0 && downloadedSize != manifest->sizeBytes)) {
        m_status = "update download size mismatch";
        companion::support::appendDebugLog("update download: size mismatch path=" + packagePath + " expected=" + std::to_string(manifest->sizeBytes) + " actual=" + std::to_string(fileSizeError ? 0 : downloadedSize));
        std::error_code errorCode;
        std::filesystem::remove(packagePath, errorCode);
        scheduleFailureRetry();
        return;
    }

    if (!verifyChecksum(packagePath, manifest->sha256)) {
        m_status = "update checksum failed";
        companion::support::appendDebugLog("update download: checksum failed path=" + packagePath + " expected_sha256=" + manifest->sha256);
        std::error_code errorCode;
        std::filesystem::remove(packagePath, errorCode);
        scheduleFailureRetry();
        return;
    }

    if (!launchUpdater(packagePath)) {
        m_status = "update launch failed";
        companion::support::appendDebugLog("update launch: failed package=" + packagePath + " current=" + m_currentVersion + " target=" + manifest->version);
        scheduleFailureRetry();
        return;
    }

    m_updateInProgress = true;
    m_updateLaunchedAt = std::chrono::steady_clock::now();
    m_status = "update launched " + manifest->version;
    companion::support::appendDebugLog("update launch: launched updater for version=" + manifest->version + " package=" + packagePath);
}

std::string UpdateCoordinator::statusSummary() const {
    return m_status;
}

bool UpdateCoordinator::isNewerVersion(const std::string& candidate, const std::string& current) {
    const auto candidateParts = parseVersion(candidate);
    const auto currentParts = parseVersion(current);
    const auto count = (std::max)(candidateParts.size(), currentParts.size());

    for (std::size_t index = 0; index < count; ++index) {
        const auto candidateValue = index < candidateParts.size() ? candidateParts[index] : 0;
        const auto currentValue = index < currentParts.size() ? currentParts[index] : 0;
        if (candidateValue == currentValue) {
            continue;
        }

        return candidateValue > currentValue;
    }

    return false;
}

bool UpdateCoordinator::shouldCheckNow() const {
    return m_lastCheck.time_since_epoch().count() == 0
        || (std::chrono::steady_clock::now() - m_lastCheck) >= kUpdateCheckInterval;
}

void UpdateCoordinator::scheduleFailureRetry() {
    const auto now = std::chrono::steady_clock::now();
    m_lastCheck = now - (kUpdateCheckInterval - kUpdateFailureRetryInterval);
}

bool UpdateCoordinator::verifyChecksum(const std::string& filePath, const std::string& expectedSha256) const {
    if (expectedSha256.empty()) {
        return false;
    }

#ifdef _WIN32
    std::ifstream input(filePath, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }

    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return false;
    }

    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
        CryptReleaseContext(provider, 0);
        return false;
    }

    std::vector<char> buffer(64 * 1024);
    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytesRead = input.gcount();
        if (bytesRead <= 0) {
            break;
        }

        if (!CryptHashData(hash, reinterpret_cast<const BYTE*>(buffer.data()), static_cast<DWORD>(bytesRead), 0)) {
            CryptDestroyHash(hash);
            CryptReleaseContext(provider, 0);
            return false;
        }
    }

    DWORD hashLength = 0;
    DWORD hashLengthSize = sizeof(hashLength);
    if (!CryptGetHashParam(hash, HP_HASHSIZE, reinterpret_cast<BYTE*>(&hashLength), &hashLengthSize, 0)) {
        CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        return false;
    }

    std::vector<BYTE> hashBytes(hashLength);
    if (!CryptGetHashParam(hash, HP_HASHVAL, hashBytes.data(), &hashLength, 0)) {
        CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        return false;
    }

    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);

    std::ostringstream out;
    out.fill('0');
    out << std::hex;
    for (const auto byte : hashBytes) {
        out.width(2);
        out << static_cast<int>(byte);
    }

    return lowercase(out.str()) == lowercase(expectedSha256);
#else
    (void) filePath;
    return lowercase(expectedSha256) == lowercase(expectedSha256);
#endif
}

bool UpdateCoordinator::launchUpdater(const std::string& packagePath) const {
#ifdef _WIN32
    const auto updaterPath = updaterBinaryPath();
    if (updaterPath.empty() || !std::filesystem::exists(updaterPath)) {
        return false;
    }

    const auto stagingDirectory = std::filesystem::path(tempDirectoryPath()) / "AIRCompanion" / "Updater";
    std::error_code errorCode;
    std::filesystem::create_directories(stagingDirectory, errorCode);
    if (errorCode) {
        return false;
    }

    const auto stagedUpdaterPath = stagingDirectory / "air_companion_updater.exe";
    std::filesystem::copy_file(updaterPath, stagedUpdaterPath, std::filesystem::copy_options::overwrite_existing, errorCode);
    if (errorCode) {
        return false;
    }

    const auto targetDir = std::filesystem::path(currentExecutablePath()).parent_path().string();
    std::wstring commandLine = L"\""
        + std::filesystem::path(stagedUpdaterPath).wstring()
        + L"\" --service-name AIRCompanion --package \""
        + std::filesystem::path(packagePath).wstring()
        + L"\" --target-dir \""
        + std::filesystem::path(targetDir).wstring()
        + L"\"";

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInformation{};

    const auto created = CreateProcessW(
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW | DETACHED_PROCESS,
        nullptr,
        nullptr,
        &startupInfo,
        &processInformation
    );

    if (!created) {
        return false;
    }

    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return true;
#else
    (void) packagePath;
    return false;
#endif
}

std::string UpdateCoordinator::currentExecutablePath() const {
#ifdef _WIN32
    std::wstring path(4096, L'\0');
    const auto copied = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (copied == 0) {
        return {};
    }

    path.resize(copied);
    const auto required = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }

    std::string converted(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, converted.data(), required, nullptr, nullptr);
    converted.resize(static_cast<std::size_t>(required - 1));
    return converted;
#else
    return {};
#endif
}

std::string UpdateCoordinator::updaterBinaryPath() const {
    auto path = std::filesystem::path(currentExecutablePath()).parent_path() / "air_companion_updater.exe";
    return path.string();
}

std::string UpdateCoordinator::stagePackagePath(const models::UpdateManifest& manifest) const {
    const auto stagingDirectory = std::filesystem::path(tempDirectoryPath()) / "AIRCompanion" / "Updates" / manifest.version;
    std::error_code errorCode;
    std::filesystem::create_directories(stagingDirectory, errorCode);
    return (stagingDirectory / "air_companion_update.zip").string();
}

}  // namespace companion::service
