#pragma once

#include <chrono>
#include <string>

#include "companion/models/UpdateManifest.h"
#include "companion/networking/CompanionApiClient.h"

namespace companion::service {

class UpdateCoordinator {
public:
    UpdateCoordinator(networking::CompanionApiClient apiClient, std::string currentVersion);

    void tick();
    std::string statusSummary() const;

    static bool isNewerVersion(const std::string& candidate, const std::string& current);

private:
    bool shouldCheckNow() const;
    bool verifyChecksum(const std::string& filePath, const std::string& expectedSha256) const;
    bool launchUpdater(const std::string& packagePath) const;
    std::string currentExecutablePath() const;
    std::string updaterBinaryPath() const;
    std::string stagePackagePath(const models::UpdateManifest& manifest) const;

    networking::CompanionApiClient m_apiClient;
    std::string m_currentVersion;
    std::string m_status{"updates idle"};
    std::chrono::steady_clock::time_point m_lastCheck{};
    std::chrono::steady_clock::time_point m_updateLaunchedAt{};
    bool m_updateInProgress{false};
};

}  // namespace companion::service
