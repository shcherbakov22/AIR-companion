#include "companion/service/Bootstrap.h"

#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace companion::service {

namespace {

void launchBrowserLoginUrl(const std::string& browserLoginUrl) {
#ifdef _WIN32
    if (browserLoginUrl.empty()) {
        return;
    }

    int wideLength = MultiByteToWideChar(CP_UTF8, 0, browserLoginUrl.c_str(), -1, nullptr, 0);
    if (wideLength <= 0) {
        return;
    }

    std::wstring wideUrl(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, browserLoginUrl.c_str(), -1, wideUrl.data(), wideLength) <= 0) {
        return;
    }

    std::wstring command = L"cmd /c start \"\" \"";
    command += wideUrl.c_str();
    command += L"\"";

    const int result = _wsystem(command.c_str());
    if (result != 0) {
        std::cerr << "AIR Companion failed to open browser login URL.\n";
    }
#else
    (void) browserLoginUrl;
#endif
}

bool rewriteExternalPlatformUrl(StoredCompanionConfig& config) {
    constexpr const char* kLocalBaseUrl = "https://192.168.11.228";

    if (config.baseUrl == "https://karatunov.net"
        || config.baseUrl == "http://karatunov.net"
        || config.baseUrl == "https://karatunov.net/"
        || config.baseUrl == "http://karatunov.net/") {
        config.baseUrl = kLocalBaseUrl;
        config.rootCaUrl.clear();
        return true;
    }

    return false;
}

}  // namespace

Bootstrap::Bootstrap(CompanionConfigStore configStore) : m_configStore(std::move(configStore)) {}

std::optional<BootstrapResult> Bootstrap::initialize() const {
    if (const auto stored = m_configStore.load(); stored.has_value()) {
        auto config = *stored;
        bool shouldSaveConfig = false;

        if (rewriteExternalPlatformUrl(config)) {
            shouldSaveConfig = true;
        }

        if (config.identity.appVersion != AIR_COMPANION_VERSION) {
            config.identity.appVersion = AIR_COMPANION_VERSION;
            shouldSaveConfig = true;
        }

        if (shouldSaveConfig) {
            (void) m_configStore.save(config);
        }

        if (!ensureTrustedRoot(config.baseUrl, config.rootCaUrl, false)) {
            return std::nullopt;
        }

        networking::CompanionApiClient apiClient(config.baseUrl);
        const auto renewResult = apiClient.renewToken(config.deviceToken);
        if (renewResult.token.has_value()) {
            auto refreshed = config;
            refreshed.deviceToken = *renewResult.token;
            (void)m_configStore.save(refreshed);
            return BootstrapResult{
                std::move(apiClient),
                std::move(refreshed),
                "loaded saved enrollment",
            };
        }

        if (renewResult.shouldClearSavedConfig) {
            (void) m_configStore.clear();
        } else {
            return BootstrapResult{
                std::move(apiClient),
                std::move(config),
                "loaded saved enrollment without token refresh",
            };
        }
    }

    if (const auto request = m_enrollmentRequestStore.load(); request.has_value()) {
        if (!ensureTrustedRoot(request->baseUrl, request->rootCaUrl, !request->rootCaUrl.empty())) {
            return std::nullopt;
        }

        const auto effectiveLabel = request->deviceLabel.empty() ? defaultDeviceLabel() : request->deviceLabel;
        const auto bootstrapped = !request->enrollmentToken.empty()
            ? initializeFromEnrollmentToken(
                request->baseUrl,
                request->enrollmentToken,
                effectiveLabel,
                request->rootCaUrl
            )
            : initializeFromCredentials(
                request->baseUrl,
                request->username,
                request->password,
                effectiveLabel,
                request->rootCaUrl
            );
        if (bootstrapped.has_value()) {
            (void) m_enrollmentRequestStore.clear();
            return bootstrapped;
        }
    }

    const auto baseUrl = envOrDefault("AIR_COMPANION_BASE_URL", "https://127.0.0.1");
    const auto enrollmentToken = envOrDefault("AIR_COMPANION_ENROLLMENT_TOKEN");
    const auto username = envOrDefault("AIR_COMPANION_USERNAME");
    const auto password = envOrDefault("AIR_COMPANION_PASSWORD");
    const auto rootCaUrl = envOrDefault("AIR_COMPANION_ROOT_CA_URL");

    if (enrollmentToken.empty() && (username.empty() || password.empty())) {
        (void) m_enrollmentRequestStore.saveTemplate();
        return std::nullopt;
    }

    if (!ensureTrustedRoot(baseUrl, rootCaUrl, !rootCaUrl.empty())) {
        return std::nullopt;
    }

    const auto deviceLabel = envOrDefault("AIR_COMPANION_DEVICE_LABEL", defaultDeviceLabel());
    const auto bootstrapped = !enrollmentToken.empty()
        ? initializeFromEnrollmentToken(baseUrl, enrollmentToken, deviceLabel, rootCaUrl)
        : initializeFromCredentials(baseUrl, username, password, deviceLabel, rootCaUrl);

    if (bootstrapped.has_value()) {
        (void) m_enrollmentRequestStore.clear();
    }

    return bootstrapped;
}

std::string Bootstrap::envOrDefault(const char* name, const std::string& fallback) {
    if (const auto* value = std::getenv(name); value != nullptr && *value != '\0') {
        return value;
    }

    return fallback;
}

std::string Bootstrap::defaultHostname() {
    return envOrDefault("COMPUTERNAME", "student-pc");
}

std::string Bootstrap::defaultDeviceLabel() {
    return defaultHostname();
}

std::string Bootstrap::randomDeviceKey() {
    std::random_device device;
    std::mt19937_64 generator(device());
    std::uniform_int_distribution<unsigned long long> distribution;

    std::ostringstream out;
    out << std::hex << distribution(generator) << distribution(generator);
    return out.str();
}

bool Bootstrap::ensureTrustedRoot(const std::string& baseUrl,
                                  const std::string& rootCaUrl,
                                  const bool required) const {
    const auto installed = m_trustedRootInstaller.ensureTrustedForBaseUrl(baseUrl, rootCaUrl);
    if (!installed && required) {
        std::cerr << "AIR Companion failed to install the configured root CA from "
                  << rootCaUrl << '\n';
    }

    return installed || !required;
}

std::optional<BootstrapResult> Bootstrap::initializeFromEnrollmentToken(
    const std::string& baseUrl,
    const std::string& enrollmentToken,
    const std::string& deviceLabel,
    const std::string& rootCaUrl
) const {
    if (baseUrl.empty() || enrollmentToken.empty()) {
        return std::nullopt;
    }

    StoredCompanionConfig config;
    config.baseUrl = baseUrl;
    config.rootCaUrl = rootCaUrl;
    config.identity.deviceId = randomDeviceKey();
    config.identity.hostname = defaultHostname();
    config.identity.deviceLabel = deviceLabel.empty() ? defaultDeviceLabel() : deviceLabel;
    config.identity.platform = "windows";
    config.identity.appVersion = AIR_COMPANION_VERSION;

    networking::CompanionApiClient apiClient(config.baseUrl);
    const auto enrollment = apiClient.claimEnrollment(enrollmentToken, config.identity);
    if (!enrollment.has_value()) {
        return std::nullopt;
    }

    config.identity = enrollment->identity;
    config.deviceToken = enrollment->deviceToken;
    (void) m_configStore.save(config);
    launchBrowserLoginUrl(enrollment->browserLoginUrl);

    return BootstrapResult{
        std::move(apiClient),
        std::move(config),
        "claimed enrollment token with AIR",
    };
}

std::optional<BootstrapResult> Bootstrap::initializeFromCredentials(
    const std::string& baseUrl,
    const std::string& username,
    const std::string& password,
    const std::string& deviceLabel,
    const std::string& rootCaUrl
) const {
    if (baseUrl.empty() || username.empty() || password.empty()) {
        return std::nullopt;
    }

    StoredCompanionConfig config;
    config.baseUrl = baseUrl;
    config.rootCaUrl = rootCaUrl;
    config.identity.deviceId = randomDeviceKey();
    config.identity.hostname = defaultHostname();
    config.identity.deviceLabel = deviceLabel.empty() ? defaultDeviceLabel() : deviceLabel;
    config.identity.platform = "windows";
    config.identity.appVersion = AIR_COMPANION_VERSION;
    config.identity.studentUsername = username;

    networking::CompanionApiClient apiClient(config.baseUrl);
    const auto enrollment = apiClient.enroll(username, password, config.identity);
    if (!enrollment.has_value()) {
        return std::nullopt;
    }

    config.identity = enrollment->identity;
    config.deviceToken = enrollment->deviceToken;
    (void) m_configStore.save(config);
    launchBrowserLoginUrl(enrollment->browserLoginUrl);

    return BootstrapResult{
        std::move(apiClient),
        std::move(config),
        "enrolled device with AIR",
    };
}

}  // namespace companion::service
