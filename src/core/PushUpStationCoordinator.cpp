#include "companion/core/PushUpStationCoordinator.h"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ctime>
#include <utility>

namespace companion::core {

namespace {
constexpr auto kHeartbeatInterval = std::chrono::seconds(5);
constexpr auto kSessionIdleTimeout = std::chrono::seconds(30);
constexpr auto kStartSyncRetryInterval = std::chrono::seconds(2);
constexpr auto kStateLogInterval = std::chrono::seconds(5);
constexpr int kMaxLaunchFailures = 3;

std::filesystem::path debugLogPath() {
#ifdef _WIN32
    if (const char* programData = std::getenv("ProgramData"); programData != nullptr && *programData != '\0') {
        return std::filesystem::path(programData) / "AIRCompanion" / "Logs" / "debug.log";
    }
#endif
    if (const char* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0') {
        return std::filesystem::path(appData) / "AIRCompanion" / "debug.log";
    }
    return std::filesystem::path(".") / "AIRCompanion" / "Logs" / "debug.log";
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
    return buffer;
}

void appendDebugLog(const std::string& line) {
    const auto logPath = debugLogPath();
    std::error_code errorCode;
    std::filesystem::create_directories(logPath.parent_path(), errorCode);

    std::ofstream output(logPath, std::ios::app);
    if (!output.is_open()) {
        return;
    }

    output << timestamp() << " " << line << '\n';
}

std::string adapterStateSummary(const adapters::PushUpCounterState& state) {
    std::ostringstream output;
    output << "connected=" << (state.connected ? "1" : "0")
           << " firmwareReady=" << (state.firmwareReady ? "1" : "0")
           << " port=" << (state.portName.empty() ? "-" : state.portName)
           << " status=" << state.status
           << " distance=" << state.distance
           << " rep=" << state.currentRep
           << " set=" << state.currentSet
           << " searchingBack=" << (state.searchingBack ? "1" : "0")
           << " working=" << (state.working ? "1" : "0")
           << " completionPending=" << (state.completionPending ? "1" : "0");
    if (!state.errorMessage.empty()) {
        output << " error=" << state.errorMessage;
    }
    return output.str();
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
    logStateSnapshot("tick");

    if (!m_adapter.state().connected) {
        if (m_session.has_value() && !m_launchedSessionId.empty()) {
            failActiveSession("Arduino disconnected.");
        } else if (m_session.has_value()) {
            appendDebugLog("push-up active/claimed session dropped because adapter is disconnected: session=" + m_session->id);
        }
        m_session.reset();
        m_launchedSessionId.clear();
        m_startSynced = false;
        m_lastStartSyncAttemptAt = {};
        m_lastProgressRep = 0;
        m_launchFailureCount = 0;
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
            logStateSnapshot("session-idle-timeout");
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
    appendDebugLog("push-up heartbeat request station=" + m_stationKey + " name=" + m_stationName);
    const auto result = m_apiClient.pushUpStationHeartbeat(m_deviceToken, m_stationKey, m_stationName);
    if (!result.has_value()) {
        appendDebugLog("push-up heartbeat failed/no-response station=" + m_stationKey);
        return;
    }

    m_pendingCount = result->pendingCount;
    appendDebugLog("push-up heartbeat ok pending=" + std::to_string(m_pendingCount)
        + " session=" + (result->currentSession.has_value() ? result->currentSession->id : std::string("none")));
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
        m_lastStartSyncAttemptAt = {};
        m_lastProgressRep = 0;
        m_lastMeaningfulActivityAt = {};
        m_launchFailureCount = 0;
    }
}

void PushUpStationCoordinator::ensureSessionClaimed() {
    if (m_session.has_value()) {
        return;
    }

    m_session = m_apiClient.pushUpStationClaimNext(m_deviceToken, m_stationKey, m_stationName);
    if (m_session.has_value()) {
        appendDebugLog("push-up session claimed: " + m_session->id
            + " student=" + m_session->studentName
            + " reps=" + std::to_string(m_session->requiredPushUps)
            + " drop=" + std::to_string(m_session->dropThreshold)
            + " upGap=" + std::to_string(m_session->upGap)
            + " downTolerance=" + std::to_string(m_session->downTolerance));
        m_launchedSessionId.clear();
        m_startSynced = false;
        m_lastStartSyncAttemptAt = {};
        m_lastProgressRep = 0;
        m_lastMeaningfulActivityAt = {};
        m_launchFailureCount = 0;
        logStateSnapshot("claimed");
    }
}

void PushUpStationCoordinator::ensureSessionLaunched() {
    if (!m_session.has_value() || !m_adapter.state().firmwareReady) {
        if (m_session.has_value()) {
            appendDebugLog("push-up launch deferred: firmware not ready session=" + m_session->id + " state=" + adapterStateSummary(m_adapter.state()));
        }
        return;
    }

    if (m_launchedSessionId != m_session->id) {
        appendDebugLog("push-up launching local session: " + m_session->id
            + " attempt=" + std::to_string(m_launchFailureCount + 1)
            + " stateBefore=" + adapterStateSummary(m_adapter.state()));
        if (!m_adapter.startSession(
            m_session->id,
            m_session->requiredPushUps,
            m_session->dropThreshold,
            m_session->upGap,
            m_session->downTolerance
        )) {
            ++m_launchFailureCount;
            appendDebugLog("push-up local launch failed: session=" + m_session->id
                + " failures=" + std::to_string(m_launchFailureCount)
                + " stateAfter=" + adapterStateSummary(m_adapter.state()));
            if (m_adapter.state().status == "error" || !m_adapter.state().errorMessage.empty()) {
                failActiveSession("Arduino refused push-up session: "
                    + (m_adapter.state().errorMessage.empty() ? m_adapter.state().status : m_adapter.state().errorMessage));
            } else if (m_launchFailureCount >= kMaxLaunchFailures) {
                failActiveSession("Arduino did not acknowledge push-up START after "
                    + std::to_string(m_launchFailureCount) + " attempts.");
            }
            return;
        }

        appendDebugLog("push-up session launched locally: " + m_session->id + " stateAfter=" + adapterStateSummary(m_adapter.state()));
        m_launchedSessionId = m_session->id;
        m_lastProgressRep = 0;
        m_lastMeaningfulActivityAt = std::chrono::steady_clock::now();
        m_lastStartSyncAttemptAt = {};
        m_launchFailureCount = 0;
    }

    if (m_startSynced) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (m_lastStartSyncAttemptAt.time_since_epoch().count() != 0
        && (now - m_lastStartSyncAttemptAt) < kStartSyncRetryInterval) {
        return;
    }

    m_lastStartSyncAttemptAt = now;
    appendDebugLog("push-up session start sync request: " + m_session->id);
    m_startSynced = m_apiClient.pushUpStationStart(m_deviceToken, m_stationKey, m_session->id);
    appendDebugLog(std::string("push-up session start sync ") + (m_startSynced ? "ok: " : "failed: ") + m_session->id);
}

void PushUpStationCoordinator::syncProgress() {
    if (!m_session.has_value() || m_launchedSessionId != m_session->id) {
        return;
    }

    const auto& state = m_adapter.state();
    if (state.currentRep <= m_lastProgressRep) {
        return;
    }

    appendDebugLog("push-up progress sync request: session=" + m_session->id
        + " rep=" + std::to_string(state.currentRep)
        + " set=" + std::to_string(state.currentSet)
        + " previousRep=" + std::to_string(m_lastProgressRep));
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
    } else {
        appendDebugLog("push-up progress sync failed: session=" + m_session->id + " rep=" + std::to_string(state.currentRep));
    }
}

bool PushUpStationCoordinator::syncCompletion() {
    if (!m_session.has_value() || m_launchedSessionId != m_session->id) {
        return false;
    }

    if (!m_adapter.state().completionPending) {
        return false;
    }

    appendDebugLog("push-up completion pending: " + m_session->id + " state=" + adapterStateSummary(m_adapter.state()));
    if (m_apiClient.pushUpStationComplete(m_deviceToken, m_stationKey, m_session->id)) {
        (void) m_adapter.consumeCompletion();
        appendDebugLog("push-up completion sync ok: " + m_session->id);
        m_session.reset();
        m_launchedSessionId.clear();
        m_startSynced = false;
        m_lastStartSyncAttemptAt = {};
        m_lastProgressRep = 0;
        m_lastMeaningfulActivityAt = {};
        m_launchFailureCount = 0;
    } else {
        appendDebugLog("push-up completion sync failed: " + m_session->id);
    }

    return true;
}

void PushUpStationCoordinator::failActiveSession(const std::string& notes) {
    if (!m_session.has_value()) {
        return;
    }

    appendDebugLog("push-up session fail: " + m_session->id + " notes=" + notes + " state=" + adapterStateSummary(m_adapter.state()));
    if (!m_launchedSessionId.empty() && m_adapter.state().connected) {
        (void) m_adapter.abortSession(m_session->id);
        m_adapter.hardReset();
    }
    (void) m_apiClient.pushUpStationFail(m_deviceToken, m_stationKey, m_session->id, notes);
    m_session.reset();
    m_launchedSessionId.clear();
    m_startSynced = false;
    m_lastStartSyncAttemptAt = {};
    m_lastProgressRep = 0;
    m_lastMeaningfulActivityAt = {};
    m_launchFailureCount = 0;
}

void PushUpStationCoordinator::logStateSnapshot(const char* reason) {
    const auto now = std::chrono::steady_clock::now();
    const auto sessionId = m_session.has_value() ? m_session->id : std::string("-");
    const auto signature = std::string("session=") + sessionId
        + " launched=" + (m_launchedSessionId.empty() ? std::string("-") : m_launchedSessionId)
        + " startSynced=" + (m_startSynced ? "1" : "0")
        + " lastProgressRep=" + std::to_string(m_lastProgressRep)
        + " launchFailures=" + std::to_string(m_launchFailureCount)
        + " pending=" + std::to_string(m_pendingCount)
        + " " + adapterStateSummary(m_adapter.state());

    if (signature == m_lastStateLogSignature
        && m_lastStateLogAt.time_since_epoch().count() != 0
        && (now - m_lastStateLogAt) < kStateLogInterval) {
        return;
    }

    m_lastStateLogAt = now;
    m_lastStateLogSignature = signature;
    appendDebugLog(std::string("push-up state ") + reason + ": " + signature);
}

}  // namespace companion::core
