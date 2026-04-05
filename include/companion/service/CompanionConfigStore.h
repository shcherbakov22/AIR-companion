#pragma once

#include <optional>
#include <string>

#include "companion/models/DeviceIdentity.h"

namespace companion::service {

struct StoredCompanionConfig {
    std::string baseUrl;
    std::string deviceToken;
    std::string rootCaUrl;
    models::DeviceIdentity identity;
};

class CompanionConfigStore {
public:
    std::optional<StoredCompanionConfig> load() const;
    bool save(const StoredCompanionConfig& config) const;
    bool clear() const;
    std::string configPath() const;
    std::string backupConfigPath() const;

private:
    static std::string configDirectory();
    static std::optional<StoredCompanionConfig> loadFromPath(const std::string& path);
    static bool saveToPath(const std::string& path, const StoredCompanionConfig& config);
    static std::string machineConfigDirectory();
};

}  // namespace companion::service
