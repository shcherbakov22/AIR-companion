#pragma once

#include <optional>
#include <string>
#include <vector>

#include "companion/models/ActivitySnapshot.h"
#include "companion/models/DeviceCommand.h"
#include "companion/models/DeviceIdentity.h"
#include "companion/models/DevicePolicy.h"
#include "companion/models/PushUpStationSession.h"
#include "companion/models/UpdateManifest.h"
#include "companion/networking/HttpClient.h"

namespace companion::networking {

struct RenewTokenResult {
    std::optional<std::string> token;
    bool shouldClearSavedConfig{false};
};

struct PushUpStationHeartbeatResult {
    bool accepted{false};
    int pendingCount{0};
    std::optional<models::PushUpStationSession> currentSession;
};

class CompanionApiClient {
public:
    CompanionApiClient(std::string baseUrl, HttpClient httpClient = {});

    std::optional<models::DeviceEnrollment> enroll(
        const std::string& username,
        const std::string& password,
        const models::DeviceIdentity& identity) const;
    std::optional<models::DeviceEnrollment> claimEnrollment(
        const std::string& enrollmentToken,
        const models::DeviceIdentity& identity) const;
    RenewTokenResult renewToken(const std::string& deviceToken) const;
    std::optional<std::string> createBrowserLoginUrl(const std::string& deviceToken) const;

    std::optional<models::DevicePolicy> fetchPolicy(const std::string& deviceToken) const;

    std::vector<models::DeviceCommand> fetchCommands(const std::string& deviceToken) const;
    bool acknowledgeCommand(const std::string& deviceToken, const std::string& commandId) const;
    bool submitCommandResult(const std::string& deviceToken,
                             const std::string& commandId,
                             bool success,
                             const std::string& output) const;

    bool sendHeartbeat(
        const std::string& deviceToken,
        const models::DeviceIdentity& identity,
        const models::ActivitySnapshot& snapshot,
        const std::string& networkState) const;
    bool sendActivity(const std::string& deviceToken, const models::ActivitySnapshot& snapshot) const;
    bool sendInstalledApps(const std::string& deviceToken, const std::vector<models::InstalledAppEntry>& apps) const;
    bool uploadScreenCapture(const std::string& deviceToken,
                             const std::string& filePath,
                             const models::ActivitySnapshot& snapshot,
                             const std::string& contentType) const;
    bool uploadCameraCapture(const std::string& deviceToken,
                             const std::string& filePath,
                             const models::ActivitySnapshot& snapshot,
                             const std::string& contentType) const;
    std::optional<PushUpStationHeartbeatResult> pushUpStationHeartbeat(
        const std::string& deviceToken,
        const std::string& stationKey,
        const std::string& stationName) const;
    std::optional<models::PushUpStationSession> pushUpStationClaimNext(
        const std::string& deviceToken,
        const std::string& stationKey,
        const std::string& stationName) const;
    bool pushUpStationStart(
        const std::string& deviceToken,
        const std::string& stationKey,
        const std::string& sessionId) const;
    bool pushUpStationProgress(
        const std::string& deviceToken,
        const std::string& stationKey,
        const std::string& sessionId,
        int currentRep,
        int currentSet) const;
    bool pushUpStationComplete(
        const std::string& deviceToken,
        const std::string& stationKey,
        const std::string& sessionId) const;
    bool pushUpStationFail(
        const std::string& deviceToken,
        const std::string& stationKey,
        const std::string& sessionId,
        const std::string& notes) const;
    std::optional<models::UpdateManifest> fetchUpdateManifest() const;
    bool downloadFile(const std::string& url, const std::string& filePath) const;
    const std::string& baseUrl() const;

private:
    std::string m_baseUrl;
    HttpClient m_httpClient;
};

}  // namespace companion::networking
