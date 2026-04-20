#include "companion/core/PushUpStationCoordinator.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <utility>

namespace companion::core {

namespace {
constexpr auto kHeartbeatInterval = std::chrono::seconds(5);
constexpr auto kSessionIdleTimeout = std::chrono::seconds(30);

void appendDebugLog(const std::string& line) {
    const char* appData = std::getenv("APPDATA");
    if (appData == nullptr || *appData == '\0') {
        return;
    }

    const auto logDirectory = std::filesystem::path(appData) / "AIRCompanion";
    std::error_code errorCode;
    std::filesystem::create_directories(logDirectory, errorCode);

    std::ofstream output(logDirectory / "debug.log", std::ios::app);
    if (!output.is_open()) {
        return;
    }

    output << line << '\n';
}
}

PushUpStationCoordinator::PushUpStationCoordinator(
    const networking::CompanionApiClient& apiClient,
    std::string deviceToken,
    models::DeviceIdentity identity,
    adapters::IPushUpCounterAdapter& adapter)
    : m_apiClient(apiClient),
      m_deviceToken(std::move(deviceToken)),
      m_identity(std::move(identity)),
      m_adapter(adapter),
      m_stationKey("pushup-" + m_identity.deviceId),
      m_stationName(m_identity.deviceLabel.empty() ? "Push-up counter" : (m_identity.deviceLabel + " push-up")) {}

void PushUpStationCoordinator::tick() {
    m_adapter.tick();

    if (!m_adapter.state().connected) {
        if (m_session.has_value() && !m_launchedSessionId.empty()) {
            failActiveSession("Arduino disconnected.");
        }
        m_session.reset();
        m_launchedSessionId.clear();
        m_startSynced = false;
        m_lastProgressRep = 0;
        m_status = "push-up disconnected";
        return;
    }

    // Completion resolves the linked violation server-side, so send it before
    // lower-priority heartbeat/progress work that can otherwise delay removal.
    const bool completionAttempted = syncCompletion();
    syncHeartbeat();
    ensureSessionClaimed();
    ensureSessionLaunched();
    syncProgress();
    if (!completionAttempted) {
        syncCompletion();
    }

    if (m_session.has_value()) {
        const auto state = m_adapter.state();
        if ((state.searchingBack || state.working || state.currentRep > 0) && m_lastMeaningfulActivityAt.time_since_epoch().count() == 0) {
            m_lastMeaningfulActivityAt = std::chrono::steady_clock::now();
        }

        if (state.searchingBack || state.working || state.currentRep > m_lastProgressRep) {
            m_lastMeaningfulActivityAt = std::chrono::steady_clock::now();
        }

        if (m_lastMeaningfulActivityAt.time_since_epoch().count() != 0
            && (std::chrono::steady_clock::now() - m_lastMeaningfulActivityAt) >= kSessionIdleTimeout) {
            failActiveSession("Push-up session timed out waiting for activity.");
        }
    }

    m_status = m_adapter.state().connected
        ? ("push-up " + m_adapter.state().status + " " + (m_session.has_value() ? ("session " + m_session->id) : "idle"))
        : "push-up disconnected";
}

std::string PushUpStationCoordinator::statusSummary() const {
    return m_status;
}

void PushUpStationCoordinator::syncHeartbeat() {
    const auto now = std::chrono::steady_clock::now();
    if (m_lastHeartbeatAt.time_since_epoch().count() != 0 && (now - m_lastHeartbeatAt) < kHeartbeatInterval) {
        return;
    }

    m_lastHeartbeatAt = now;
    const auto result = m_apiClient.pushUpStationHeartbeat(m_deviceToken, m_stationKey, m_stationName);
    if (!result.has_value()) {
        return;
    }

    m_pendingCount = result->pendingCount;
    if (result->currentSession.has_value()) {
        if (!m_session.has_value() || m_session->id != result->currentSession->id || m_session->status != result->currentSession->status) {
            appendDebugLog("push-up heartbeat current session: " + result->currentSession->id + " status=" + result->currentSession->status);
        }
        m_session = result->currentSession;
    } else if (m_session.has_value()) {
        appendDebugLog("push-up heartbeat cleared local stale session: " + m_session->id);
        if (!m_launchedSessionId.empty() && m_adapter.state().connected) {
            (void) m_adapter.abortSession(m_session->id);
        }
        m_session.reset();
        m_launchedSessionId.clear();
        m_startSynced = false;
        m_lastProgressRep = 0;
        m_lastMeaningfulActivityAt = {};
    }
}

void PushUpStationCoordinator::ensureSessionClaimed() {
    if (m_session.has_value()) {
        return;
    }

    m_session = m_apiClient.pushUpStationClaimNext(m_deviceToken, m_stationKey, m_stationName);
    if (m_session.has_value()) {
        appendDebugLog("push-up session claimed: " + m_session->id + " reps=" + std::to_string(m_session->requiredPushUps));
        m_launchedSessionId.clear();
        m_startSynced = false;
        m_lastProgressRep = 0;
        m_lastMeaningfulActivityAt = {};
    }
}

void PushUpStationCoordinator::ensureSessionLaunched() {
    if (!m_session.has_value() || !m_adapter.state().firmwareReady) {
        return;
    }

    if (m_launchedSessionId == m_session->id) {
        return;
    }

    if (!m_adapter.startSession(
        m_session->id,
        m_session->requiredPushUps,
        m_session->dropThreshold,
        m_session->upGap,
        m_session->downTolerance
    )) {
        return;
    }

    appendDebugLog("push-up session launched locally: " + m_session->id);
    m_launchedSessionId = m_session->id;
    m_lastProgressRep = 0;
    m_lastMeaningfulActivityAt = std::chrono::steady_clock::now();

    if (!m_startSynced) {
        m_startSynced = m_apiClient.pushUpStationStart(m_deviceToken, m_stationKey, m_session->id);
        appendDebugLog(std::string("push-up session start sync ") + (m_startSynced ? "ok: " : "failed: ") + m_session->id);
    }
}

void PushUpStationCoordinator::syncProgress() {
    if (!m_session.has_value() || m_launchedSessionId != m_session->id) {
        return;
    }

    const auto& state = m_adapter.state();
    if (state.currentRep <= m_lastProgressRep) {
        return;
    }

    if (m_apiClient.pushUpStationProgress(
        m_deviceToken,
        m_stationKey,
        m_session->id,
        state.currentRep,
        state.currentSet
    )) {
        appendDebugLog("push-up progress sync ok: session=" + m_session->id + " rep=" + std::to_string(state.currentRep) + " set=" + std::to_string(state.currentSet));
        m_lastProgressRep = state.currentRep;
        m_lastMeaningfulActivityAt = std::chrono::steady_clock::now();
    }
}

bool PushUpStationCoordinator::syncCompletion() {
    if (!m_session.has_value() || m_launchedSessionId != m_session->id) {
        return false;
    }

    if (!m_adapter.state().completionPending) {
        return false;
    }

    appendDebugLog("push-up completion pending: " + m_session->id);
    if (m_apiClient.pushUpStationComplete(m_deviceToken, m_stationKey, m_session->id)) {
        (void) m_adapter.consumeCompletion();
        appendDebugLog("push-up completion sync ok: " + m_session->id);
        m_session.reset();
        m_launchedSessionId.clear();
        m_startSynced = false;
        m_lastProgressRep = 0;
        m_lastMeaningfulActivityAt = {};
    } else {
        appendDebugLog("push-up completion sync failed: " + m_session->id);
    }

    return true;
}

void PushUpStationCoordinator::failActiveSession(const std::string& notes) {
    if (!m_session.has_value()) {
        return;
    }

    appendDebugLog("push-up session fail: " + m_session->id + " notes=" + notes);
    if (!m_launchedSessionId.empty() && m_adapter.state().connected) {
        (void) m_adapter.abortSession(m_session->id);
        m_adapter.hardReset();
    }
    (void) m_apiClient.pushUpStationFail(m_deviceToken, m_stationKey, m_session->id, notes);
    m_session.reset();
    m_launchedSessionId.clear();
    m_startSynced = false;
    m_lastProgressRep = 0;
    m_lastMeaningfulActivityAt = {};
}

}  // namespace companion::core
