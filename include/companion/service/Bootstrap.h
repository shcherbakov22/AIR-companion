#pragma once

#include <optional>
#include <string>

#include "companion/networking/CompanionApiClient.h"
#include "companion/service/CompanionConfigStore.h"
#include "companion/service/EnrollmentRequestStore.h"
#include "companion/service/TrustedRootInstaller.h"

namespace companion::service {

struct BootstrapResult {
    networking::CompanionApiClient apiClient;
    StoredCompanionConfig config;
    std::string status;
};

class Bootstrap {
public:
    explicit Bootstrap(CompanionConfigStore configStore = {});

    std::optional<BootstrapResult> initialize() const;

private:
    static std::string envOrDefault(const char* name, const std::string& fallback = {});
    static std::string defaultHostname();
    static std::string defaultDeviceLabel();
    static std::string randomDeviceKey();
    bool ensureTrustedRoot(const std::string& baseUrl, const std::string& rootCaUrl = {}, bool required = false) const;
    std::optional<BootstrapResult> initializeFromCredentials(
        const std::string& baseUrl,
        const std::string& username,
        const std::string& password,
        const std::string& deviceLabel,
        const std::string& rootCaUrl
    ) const;

    CompanionConfigStore m_configStore;
    EnrollmentRequestStore m_enrollmentRequestStore;
    TrustedRootInstaller m_trustedRootInstaller;
};

}  // namespace companion::service
