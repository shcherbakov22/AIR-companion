#include "FakeCompanionApiClient.h"

namespace companion::test {

FakeCompanionApiClient::FakeCompanionApiClient()
    : networking::CompanionApiClient("http://test.local", networking::HttpClient{}) {}

void FakeCompanionApiClient::setHeartbeatResponse(
    std::optional<networking::PushUpStationHeartbeatResult> response) {
    m_heartbeatResponse = std::move(response);
}

void FakeCompanionApiClient::setClaimNextResponse(std::optional<models::PushUpStationSession> session) {
    m_claimNextResponse = std::move(session);
}

void FakeCompanionApiClient::setStartResponse(bool success) {
    m_startResponse = success;
}

void FakeCompanionApiClient::setProgressResponse(bool success) {
    m_progressResponse = success;
}

void FakeCompanionApiClient::setCompleteResponse(bool success) {
    m_completeResponse = success;
}

void FakeCompanionApiClient::setFailResponse(bool success) {
    m_failResponse = success;
}

std::optional<networking::PushUpStationHeartbeatResult>
FakeCompanionApiClient::pushUpStationHeartbeat(
    const std::string& deviceToken,
    const std::string& stationKey,
    const std::string& stationName) const {
    m_calls.push_back({PushUpStationApiCall::Kind::Heartbeat, deviceToken, stationKey, stationName, {}, 0, 0, {}});
    return m_heartbeatResponse;
}

std::optional<models::PushUpStationSession>
FakeCompanionApiClient::pushUpStationClaimNext(
    const std::string& deviceToken,
    const std::string& stationKey,
    const std::string& stationName) const {
    m_calls.push_back({PushUpStationApiCall::Kind::ClaimNext, deviceToken, stationKey, stationName, {}, 0, 0, {}});
    auto result = m_claimNextResponse;
    m_claimNextResponse = std::nullopt;
    return result;
}

bool FakeCompanionApiClient::pushUpStationStart(
    const std::string& deviceToken,
    const std::string& stationKey,
    const std::string& sessionId) const {
    m_calls.push_back({PushUpStationApiCall::Kind::Start, deviceToken, stationKey, {}, sessionId, 0, 0, {}});
    return m_startResponse;
}

bool FakeCompanionApiClient::pushUpStationProgress(
    const std::string& deviceToken,
    const std::string& stationKey,
    const std::string& sessionId,
    int currentRep,
    int currentSet) const {
    m_calls.push_back({PushUpStationApiCall::Kind::Progress, deviceToken, stationKey, {}, sessionId, currentRep, currentSet, {}});
    return m_progressResponse;
}

bool FakeCompanionApiClient::pushUpStationComplete(
    const std::string& deviceToken,
    const std::string& stationKey,
    const std::string& sessionId) const {
    m_calls.push_back({PushUpStationApiCall::Kind::Complete, deviceToken, stationKey, {}, sessionId, 0, 0, {}});
    return m_completeResponse;
}

bool FakeCompanionApiClient::pushUpStationFail(
    const std::string& deviceToken,
    const std::string& stationKey,
    const std::string& sessionId,
    const std::string& notes) const {
    m_calls.push_back({PushUpStationApiCall::Kind::Fail, deviceToken, stationKey, {}, sessionId, 0, 0, notes});
    return m_failResponse;
}

}  // namespace companion::test
