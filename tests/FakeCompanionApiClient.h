#pragma once

#include "companion/networking/CompanionApiClient.h"

#include <optional>
#include <string>
#include <vector>

namespace companion::test {

struct PushUpStationApiCall {
    enum class Kind {
        Heartbeat,
        ClaimNext,
        Start,
        Progress,
        Complete,
        Fail,
    } kind;
    std::string deviceToken;
    std::string stationKey;
    std::string stationName;
    std::string sessionId;
    int progressRep{0};
    int progressSet{0};
    std::string failNotes;
};

class FakeCompanionApiClient : public networking::CompanionApiClient {
public:
    FakeCompanionApiClient();

    // Override push-up station API methods
    std::optional<networking::PushUpStationHeartbeatResult> pushUpStationHeartbeat(
        const std::string& deviceToken,
        const std::string& stationKey,
        const std::string& stationName) const override;
    std::optional<models::PushUpStationSession> pushUpStationClaimNext(
        const std::string& deviceToken,
        const std::string& stationKey,
        const std::string& stationName) const override;
    bool pushUpStationStart(
        const std::string& deviceToken,
        const std::string& stationKey,
        const std::string& sessionId) const override;
    bool pushUpStationProgress(
        const std::string& deviceToken,
        const std::string& stationKey,
        const std::string& sessionId,
        int currentRep,
        int currentSet) const override;
    bool pushUpStationComplete(
        const std::string& deviceToken,
        const std::string& stationKey,
        const std::string& sessionId) const override;
    bool pushUpStationFail(
        const std::string& deviceToken,
        const std::string& stationKey,
        const std::string& sessionId,
        const std::string& notes) const override;

    // Configure next-call responses
    void setHeartbeatResponse(std::optional<networking::PushUpStationHeartbeatResult> response);
    void setClaimNextResponse(std::optional<models::PushUpStationSession> session);
    void setStartResponse(bool success);
    void setProgressResponse(bool success);
    void setCompleteResponse(bool success);
    void setFailResponse(bool success);

    // Inspect recorded calls
    const std::vector<PushUpStationApiCall>& calls() const { return m_calls; }
    void clearCalls() { m_calls.clear(); }

private:
    mutable std::vector<PushUpStationApiCall> m_calls;

    mutable std::optional<networking::PushUpStationHeartbeatResult> m_heartbeatResponse;
    mutable std::optional<models::PushUpStationSession> m_claimNextResponse;
    mutable bool m_startResponse{true};
    mutable bool m_progressResponse{true};
    mutable bool m_completeResponse{true};
    mutable bool m_failResponse{true};
};

}  // namespace companion::test
