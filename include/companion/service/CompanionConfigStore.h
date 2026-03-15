#pragma once

#include <optional>
#include <string>

#include "companion/models/DeviceIdentity.h"

namespace companion::service {

struct StoredCompanionConfig {
    std::string baseUrl;
    std::string deviceToken;
    models::DeviceIdentity identity;
};

class CompanionConfigStore {
public:
    std::optional<StoredCompanionConfig> load() const;
    bool save(const StoredCompanionConfig& config) const;
    std::string configPath() const;

private:
    static std::string configDirectory();
};

}  // namespace companion::service
