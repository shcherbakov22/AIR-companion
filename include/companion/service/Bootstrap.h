#pragma once

#include <optional>
#include <string>

#include "companion/networking/CompanionApiClient.h"
#include "companion/service/CompanionConfigStore.h"

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

    CompanionConfigStore m_configStore;
};

}  // namespace companion::service
