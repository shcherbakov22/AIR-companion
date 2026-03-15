#include "companion/service/Bootstrap.h"

#include <cstdlib>
#include <random>
#include <sstream>

namespace companion::service {

Bootstrap::Bootstrap(CompanionConfigStore configStore) : m_configStore(std::move(configStore)) {}

std::optional<BootstrapResult> Bootstrap::initialize() const {
    if (const auto stored = m_configStore.load(); stored.has_value()) {
        networking::CompanionApiClient apiClient(stored->baseUrl);
        if (const auto renewedToken = apiClient.renewToken(stored->deviceToken); renewedToken.has_value()) {
            auto refreshed = *stored;
            refreshed.deviceToken = *renewedToken;
            (void)m_configStore.save(refreshed);
            return BootstrapResult{
                std::move(apiClient),
                std::move(refreshed),
                "loaded saved enrollment",
            };
        }

        return BootstrapResult{
            std::move(apiClient),
            *stored,
            "loaded saved enrollment",
        };
    }

    const auto baseUrl = envOrDefault("AIR_COMPANION_BASE_URL", "https://127.0.0.1");
    const auto username = envOrDefault("AIR_COMPANION_USERNAME");
    const auto password = envOrDefault("AIR_COMPANION_PASSWORD");

    if (username.empty() || password.empty()) {
        return std::nullopt;
    }

    StoredCompanionConfig config;
    config.baseUrl = baseUrl;
    config.identity.deviceId = randomDeviceKey();
    config.identity.hostname = defaultHostname();
    config.identity.deviceLabel = envOrDefault("AIR_COMPANION_DEVICE_LABEL", defaultDeviceLabel());
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
    (void)m_configStore.save(config);

    return BootstrapResult{
        std::move(apiClient),
        std::move(config),
        "enrolled device with AIR",
    };
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

}  // namespace companion::service
