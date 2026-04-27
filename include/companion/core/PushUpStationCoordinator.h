#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "companion/adapters/IPushUpCounterAdapter.h"
#include "companion/models/DeviceIdentity.h"
#include "companion/models/PushUpStationSession.h"
#include "companion/networking/CompanionApiClient.h"

namespace companion::core {

class PushUpStationCoordinator {
public:
    PushUpStationCoordinator(
        const networking::CompanionApiClient& apiClient,
        std::string deviceToken,
        models::DeviceIdentity identity,
        adapters::IPushUpCounterAdapter& adapter);

    void tick();
    std::string statusSummary() const;

private:
    void syncHeartbeat();
    void ensureSessionClaimed();
    void ensureSessionLaunched();
    void syncProgress();
    bool syncCompletion();
    void failActiveSession(const std::string& notes);

    const networking::CompanionApiClient& m_apiClient;
    std::string m_deviceToken;
    models::DeviceIdentity m_identity;
    adapters::IPushUpCounterAdapter& m_adapter;
    std::string m_stationKey;
    std::string m_stationName;
    std::optional<models::PushUpStationSession> m_session;
    std::string m_launchedSessionId;
    int m_lastProgressRep{0};
    int m_pendingCount{0};
    bool m_startSynced{false};
    std::chrono::steady_clock::time_point m_lastHeartbeatAt{};
    std::chrono::steady_clock::time_point m_lastStartSyncAttemptAt{};
    std::chrono::steady_clock::time_point m_lastMeaningfulActivityAt{};
    std::string m_status{"push-up idle"};
};

}  // namespace companion::core
