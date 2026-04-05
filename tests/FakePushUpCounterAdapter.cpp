#include "FakePushUpCounterAdapter.h"

#include <chrono>
#include <thread>

namespace companion::test {

void FakePushUpCounterAdapter::tick() {
    // Simulate async rep progression in test mode
    // In real tests, you drive state explicitly via simulateRepIncrement etc.
}

const adapters::PushUpCounterState& FakePushUpCounterAdapter::state() const {
    return m_state;
}

bool FakePushUpCounterAdapter::startSession(
    const std::string& sessionId,
    int totalReps,
    int /*dropThreshold*/,
    int /*upGap*/,
    int /*downTolerance*/
) {
    if (!m_state.connected || !m_state.firmwareReady) {
        return false;
    }

    m_currentSessionId = sessionId;
    m_totalReps = totalReps;
    m_state.currentRep = 0;
    m_state.currentSet = 1;
    m_state.completionPending = false;
    m_state.searchingBack = false;
    m_state.working = false;
    m_state.status = "ready";
    return true;
}

bool FakePushUpCounterAdapter::abortSession(const std::string& /*sessionId*/) {
    if (!m_state.connected) {
        return false;
    }

    m_currentSessionId.clear();
    m_state.currentRep = 0;
    m_state.currentSet = 1;
    m_state.completionPending = false;
    m_state.searchingBack = false;
    m_state.working = false;
    m_state.status = "idle";
    return true;
}

bool FakePushUpCounterAdapter::consumeCompletion() {
    if (!m_state.completionPending) {
        return false;
    }

    m_state.completionPending = false;
    return true;
}

void FakePushUpCounterAdapter::setConnected(bool connected) {
    m_state.connected = connected;
    if (!connected) {
        m_state.firmwareReady = false;
        m_state.status = "disconnected";
        m_state.searchingBack = false;
        m_state.working = false;
        m_state.currentRep = 0;
        m_state.currentSet = 1;
        m_state.completionPending = false;
        m_currentSessionId.clear();
    }
}

void FakePushUpCounterAdapter::setFirmwareReady(bool ready) {
    m_state.firmwareReady = ready;
    if (ready && m_state.connected) {
        m_state.status = m_currentSessionId.empty() ? "ready" : m_state.status;
    }
}

void FakePushUpCounterAdapter::setTestMode(bool testMode) {
    m_testMode = testMode;
}

void FakePushUpCounterAdapter::simulateRepIncrement(int rep, int set) {
    m_state.currentRep = rep;
    m_state.currentSet = set;
    if (rep > 0) {
        m_state.searchingBack = false;
        m_state.working = true;
        m_state.status = "working";
    }
    if (rep >= m_totalReps && m_totalReps > 0) {
        simulateCompletion();
    }
}

void FakePushUpCounterAdapter::simulateCompletion() {
    m_state.searchingBack = false;
    m_state.working = false;
    m_state.status = "complete";
    m_state.completionPending = true;
}

void FakePushUpCounterAdapter::simulateDisconnection() {
    setConnected(false);
}

void FakePushUpCounterAdapter::setPortName(const std::string& port) {
    m_state.portName = port;
}

}  // namespace companion::test
